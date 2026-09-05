#define SDL_MAIN_USE_CALLBACKS
#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"

#include "GL/glew.h"
#include "SDL3/SDL_opengl.h"
#include "gl/GLU.h"

#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "glm/mat4x4.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/scalar_constants.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/common.hpp"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl3.h"
#include "imgui/imgui_impl_opengl3.h"

#include "SampleRange.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <cmath>

#define STB_IMAGE_IMPLEMENTATION 
#define STBI_ONLY_PNG
#include "stb_image.h"

#define SQL_ALL_SAFETIES_ON
#pragma warning(push)
#pragma warning(disable:26495)
#pragma warning(disable:26439)
#include <sol/sol.hpp>
#pragma warning(pop)

#include "Shader.h"

// Debug utility macro
#define LogLastGLError() { GLenum err = glGetError(); if (err != GL_NO_ERROR) SDL_Log("OpenGL Error: %s", gluErrorString(err)); }

// Performance tracking
SampleRange<double, 200> drawSamples;
SampleRange<double, 200> frameSamples;

// ─── GPU timing ──────────────────────────────────────────────────────────────
// Double-buffered GL_TIME_ELAPSED queries. We write into one query while
// reading last frame's result from the other, so the CPU never blocks waiting
// on the GPU (a single query read back the same frame would stall the pipeline
// and make the measurement lie about real-world cost).
SampleRange<double, 200> gpuSamples;
GLuint gpuQueries[2] = { 0, 0 };
int    gpuQueryIndex = 0;
bool   gpuQueriesReady = false;
// Under load the GPU can fall far enough behind that a query is not ready on
// the frame we look for it, so we record fewer samples than frames. Averaging
// over the full ring would divide by 200 and count never-written zeros, which
// under-reports badly. Track how many slots actually hold data.
int    gpuValidSamples = 0;

double GpuAverageMs()
{
    if (gpuValidSamples <= 0) return 0.0;
    const int n = (gpuValidSamples < 200) ? gpuValidSamples : 200;
    double sum = 0.0;
    for (int i = 0; i < n; ++i) sum += gpuSamples.samples[i];
    return sum / n;
}

// ─── Scene complexity counters ───────────────────────────────────────────────
// Reset at the top of every frame, incremented at each draw site.
unsigned int frameDrawCalls = 0;
unsigned int frameTriangles = 0;

// Shader management
Shader basicShader;
Shader reflectionShader;

// Reflection sphere (forward-declared, defined after Mesh struct)
GLuint cubemapTexture = 0;
GLuint cubemapFBO = 0;
GLuint cubemapDepthRBO = 0;
const int CUBEMAP_SIZE = 256;
bool showReflectionSphere = true;
glm::vec3 reflSpherePos = { 0.0f, 1.0f, 2.0f };
float reflSphereScale = 0.5f;
float reflectivity = 0.95f;

// Triangle rendering globals
GLuint triangleVAO = 0, triangleVBO = 0;
bool showTriangle = true; 
std::string vertPath = "assets/shaders/basicVert.glsl";
std::string fragPath = "assets/shaders/basicFrag.glsl";

// Vertex structure for 3D rendering
struct Vertex
{
    glm::vec3 position;
    glm::u8vec4 color;
    glm::vec2 texcoord;
    glm::vec3 normal;
};

// Mesh structure for 3D object rendering
struct Mesh
{
    GLenum meshType = 0;
    int vertexCount = 0;
    int indexCount = 0;

    GLuint vertexBufferObject = 0;
    GLuint indexBufferObject = 0;
    GLuint vertexArrayObject = 0; 

    void LoadFromFile(const char* path)
    {
        Assimp::Importer importer;

        const aiScene* scene = importer.ReadFile(path,
            aiProcess_Triangulate |
            aiProcess_JoinIdenticalVertices |
            aiProcess_SortByPType);

        if (scene && scene->mNumMeshes > 0)
        {
            auto* mesh = scene->mMeshes[0];

            meshType = GL_TRIANGLES;
            vertexCount = mesh->mNumVertices;
            indexCount = mesh->mNumFaces * 3;

            Vertex* vertices = (Vertex*)SDL_malloc(sizeof(Vertex) * vertexCount);
            Uint32* indices = (Uint32*)SDL_malloc(sizeof(Uint32) * indexCount);

            // Set up indices
            for (int f = 0; f < mesh->mNumFaces; ++f)
            {
                auto& face = mesh->mFaces[f];
                for (int c = 0; c < 3; ++c)
                {
                    indices[(f * 3) + c] = face.mIndices[c];
                }
            }

            // Set up vertices
            for (int i = 0; i < vertexCount; ++i)
            {
                auto& vertex = vertices[i];

                // Position
                auto& aiVertex = mesh->mVertices[i];
                vertex.position = glm::vec3(aiVertex.x, aiVertex.y, aiVertex.z);

                // Color (if available)
                if (mesh->HasVertexColors(0))
                {
                    const auto& aiColor = mesh->mColors[0][i];
                    vertex.color = glm::u8vec4(
                        (glm::uint8)(aiColor.r * 255.0f),
                        (glm::uint8)(aiColor.g * 255.0f),
                        (glm::uint8)(aiColor.b * 255.0f),
                        (glm::uint8)(aiColor.a * 255.0f)
                    );
                }
                else
                {
                    vertex.color = glm::u8vec4(255, 255, 255, 255);
                }

                // Texture coordinates (if available)
                if (mesh->HasTextureCoords(0))
                {
                    const auto& aiTextcoord = mesh->mTextureCoords[0][i];
                    vertex.texcoord = glm::vec2(aiTextcoord.x, aiTextcoord.y);
                }
                else
                {
                    vertex.texcoord = glm::vec2(0, 0);
                }

                // Normal (if available)
                if (mesh->HasNormals())
                {
                    const auto& aiNormal = mesh->mNormals[i];
                    vertex.normal = glm::vec3(aiNormal.x, aiNormal.y, aiNormal.z);
                }
                else
                {
                    vertex.normal = glm::vec3(0, 0, 1);
                }
            }

            // Create OpenGL buffers with VAO for OpenGL 3.3
            glGenVertexArrays(1, &vertexArrayObject);
            glGenBuffers(1, &vertexBufferObject);
            glGenBuffers(1, &indexBufferObject);

            // Bind VAO first
            glBindVertexArray(vertexArrayObject);

            // Set up vertex buffer
            glBindBuffer(GL_ARRAY_BUFFER, vertexBufferObject);
            glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * vertexCount, vertices, GL_STATIC_DRAW);

            // Set up attribute pointers-------------------------------
            // Position
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
            // Color
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)offsetof(Vertex, color));
            // Texcoord
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texcoord));
            // Normal
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));

            // Set up index buffer
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBufferObject);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(Uint32) * indexCount, indices, GL_STATIC_DRAW);

            // Unbind but keep the element buffer bound to the VAO
            glBindVertexArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            SDL_free(vertices);
            SDL_free(indices);
        }
    }

    void Unload()
    {
        if (vertexArrayObject != 0)
        {
            glDeleteVertexArrays(1, &vertexArrayObject);
            vertexArrayObject = 0;
        }

        if (vertexBufferObject != 0)
        {
            glDeleteBuffers(1, &vertexBufferObject);
            vertexBufferObject = 0;
        }

        if (indexBufferObject != 0)
        {
            glDeleteBuffers(1, &indexBufferObject);
            indexBufferObject = 0;
        }
    }
};

// Reflection sphere mesh (must be after Mesh struct)
Mesh sphereMesh;

// Function to draw a mesh using modern OpenGL
void DrawMesh(const Mesh& mesh)
{
    if (mesh.vertexArrayObject == 0 || mesh.indexBufferObject == 0)
        return;

    glBindVertexArray(mesh.vertexArrayObject);
    glDrawElements(mesh.meshType, mesh.indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    // Scene complexity accounting. Only triangle topologies contribute a
    // triangle count; lines and points still cost a draw call.
    ++frameDrawCalls;
    if (mesh.meshType == GL_TRIANGLES)
        frameTriangles += mesh.indexCount / 3;
    else if (mesh.meshType == GL_TRIANGLE_STRIP || mesh.meshType == GL_TRIANGLE_FAN)
        frameTriangles += (mesh.indexCount >= 3) ? mesh.indexCount - 2 : 0;
}

// Function to load a texture from file
GLuint loadTexture(const char* path)
{
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Load image data
    int width, height, nrChannels;
    unsigned char* data = stbi_load(path, &width, &height, &nrChannels, 0);

    if (data)
    {
        // Determine format based on number of channels
        GLenum format = GL_RGB;
        if (nrChannels == 1)
            format = GL_RED;
        else if (nrChannels == 3)
            format = GL_RGB;
        else if (nrChannels == 4)
            format = GL_RGBA;

        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        stbi_image_free(data);
        SDL_Log("Texture loaded successfully: %s (%dx%d, %d channels)", path, width, height, nrChannels);
    }
    else
    {
        SDL_Log("Failed to load texture: %s", path);
        stbi_image_free(data);
    }

    return textureID;
}

// Mesh instance structure
struct MeshInstance
{
    std::string name = "";
    glm::vec3 position = { 0.0f, 0.0f, 0.0f };
    glm::vec3 rotation = { 0.0f, 0.0f, 0.0f };
    glm::vec3 scale = { 1.0f, 1.0f, 1.0f };
    glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    Mesh* mesh = nullptr;
    GLuint textureID = 0;
    bool useTexture = false;
    bool visible = true;
};

// Application context
struct AppContext
{
    SDL_Window* window;
    SDL_GLContext context;
};

// Light structure
struct Light
{
    bool enabled = false;
    glm::vec4 position = { -1.0f, 4.0f, 4.0f, 1.0f };
    glm::vec3 direction = { 0.0f, 0.0f, 0.0f };
    glm::vec4 ambientColor = { 0.0f, 0.0f, 0.0f, 1.0f };
    glm::vec4 diffuseColor = { 0.95f, 0.9f, 0.99f, 1.0f };
    glm::vec4 specularColor = { 0.95f, 0.9f, 0.99f, 1.0f };
    float intensity = 1.0f;
    float attenuationConstant = 1.0f;
    float attenuationLinear = 0.0f;
    float attenuationQuadratic = 0.0f;
};

// Utility functions for Lua table conversion
glm::vec3 vec3FromTable(sol::optional<sol::table> obtTable)
{
    if (obtTable != sol::nullopt)
    {
        sol::table table = obtTable.value();
        return { table.get_or(1, 0.0f), table.get_or(2, 0.0f), table.get_or(3, 0.0f) };
    }
    return glm::vec3(0, 0, 0);
}

glm::vec4 vec4FromTable(sol::optional<sol::table> obtTable)
{
    if (obtTable != sol::nullopt)
    {
        sol::table table = obtTable.value();
        return { table.get_or(1, 0.0f), table.get_or(2, 0.0f), table.get_or(3, 0.0f), table.get_or(4, 1.0f) };
    }
    return glm::vec4(1, 1, 1, 1); 
}

// Global variables
const int MAX_LIGHTS = 8;
Light lights[MAX_LIGHTS];
int currentLightIndex = 0;

std::vector<MeshInstance> meshInstances;
std::unordered_map<std::string, Mesh*> meshes;
std::unordered_map<std::string, GLuint> textures;
glm::vec4 globalAmbientColor = { 0.2f, 0.2f, 0.2f, 1.0f };

ImGuiContext* imguiContext;
const bool* keyboardState = nullptr;
bool showLightGizmos = true;
GLuint lightGizmoVAO = 0, lightGizmoVBO = 0;

// Quad mesh for rendering
Vertex quadVertices[] = {
    {
        {-0.5f, 0.5f, 0.0f},
        {0, 56, 168, 255},
        {0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f}
    },
    {
        {-0.5f, -0.5f, 0.0f},
        {0, 56, 168, 255},
        {0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    },
    {
        {0.5f, -0.5f, 0.0f},
        {0, 56, 168, 255},
        {1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    },
    {
        {0.5f, 0.5f, 0.0f},
        {0, 56, 168, 255},
        {1.0f, 1.0f},
        {0.0f, 0.0f, 1.0f}
    },
};

Uint32 quadIndices[] = {
    0, 1, 2,
    0, 2, 3
};

Mesh quadMesh;

// Initialize quad mesh with VAO
void InitializeQuadMesh()
{
    quadMesh.meshType = GL_TRIANGLES;
    quadMesh.vertexCount = 4;
    quadMesh.indexCount = 6;

    // Create VAO, VBO, and IBO
    glGenVertexArrays(1, &quadMesh.vertexArrayObject);
    glGenBuffers(1, &quadMesh.vertexBufferObject);
    glGenBuffers(1, &quadMesh.indexBufferObject);

    // Bind VAO
    glBindVertexArray(quadMesh.vertexArrayObject);

    // Set up vertex buffer
    glBindBuffer(GL_ARRAY_BUFFER, quadMesh.vertexBufferObject);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    // Position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    // Color
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)offsetof(Vertex, color));
    // Texcoord
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texcoord));
    // Normal
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));

    // Set up index buffer
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadMesh.indexBufferObject);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIndices), quadIndices, GL_STATIC_DRAW);

    // Unbind
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// Function to load shader from file and compile it
GLuint loadAndCompileShader(GLenum shaderType, const char* filePath)
{
    char* shaderSource = (char*)SDL_LoadFile(filePath, nullptr);
    if (shaderSource == nullptr)
    {
        SDL_Log("Failed to load shader file: %s", filePath);
        return 0;
    }

    GLuint shader = glCreateShader(shaderType);
    glShaderSource(shader, 1, &shaderSource, NULL);
    glCompileShader(shader);

    // Check for shader compile errors
    GLint success;
    GLchar infoLog[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        SDL_Log("Shader compilation error in %s: %s", filePath, infoLog);
    }

    SDL_free(shaderSource);
    return shader;
}

// Function to create shader program from GLSL files
GLuint createShaderProgram()
{
    // Load vertex and fragment shaders from files
    GLuint vertexShader = loadAndCompileShader(GL_VERTEX_SHADER, "assets/shaders/basicVert.glsl");
    GLuint fragmentShader = loadAndCompileShader(GL_FRAGMENT_SHADER, "assets/shaders/basicFrag.glsl");

    if (vertexShader == 0 || fragmentShader == 0)
    {
        SDL_Log("Failed to load shaders from files");

        if (vertexShader != 0) glDeleteShader(vertexShader);
        if (fragmentShader != 0) glDeleteShader(fragmentShader);

        return 0;
    }

    // Create shader program
    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    // Check for linking errors
    GLint success;
    GLchar infoLog[512];
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success)
    {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        SDL_Log("Shader program linking error: %s", infoLog);
    }

    // Delete shaders as they're linked into the program and no longer needed
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return shaderProgram;
}

// Create a modern OpenGL grid
GLuint gridVAO = 0, gridVBO = 0;

void InitializeGrid(int gridPoints, float gridSize)
{
    const int lineCount = gridPoints * 2;
    const int vertexCount = lineCount * 2;
    float* vertices = new float[vertexCount * 3];

    int idx = 0;
    float halfSize = gridSize * 0.5f;
    float step = gridSize / (gridPoints - 1);

    // Create grid lines along X axis
    for (int i = 0; i < gridPoints; ++i)
    {
        float pos = -halfSize + i * step;

        // Line start
        vertices[idx++] = pos;
        vertices[idx++] = 0.0f;
        vertices[idx++] = -halfSize;

        // Line end
        vertices[idx++] = pos;
        vertices[idx++] = 0.0f;
        vertices[idx++] = halfSize;
    }

    // Create grid lines along Z axis
    for (int i = 0; i < gridPoints; ++i)
    {
        float pos = -halfSize + i * step;

        // Line start
        vertices[idx++] = -halfSize;
        vertices[idx++] = 0.0f;
        vertices[idx++] = pos;

        // Line end
        vertices[idx++] = halfSize;
        vertices[idx++] = 0.0f;
        vertices[idx++] = pos;
    }

    // Create VAO and VBO
    glGenVertexArrays(1, &gridVAO);
    glGenBuffers(1, &gridVBO);

    // Bind VAO
    glBindVertexArray(gridVAO);

    // Bind and set up VBO
    glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
    glBufferData(GL_ARRAY_BUFFER, vertexCount * 3 * sizeof(float), vertices, GL_STATIC_DRAW);

    // Set up vertex attributes
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Unbind
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    delete[] vertices;
}

// Generate a UV sphere mesh
void GenerateSphereMesh(Mesh& mesh, int stacks, int slices, float radius)
{
    std::vector<Vertex> vertices;
    std::vector<Uint32> indices;

    for (int i = 0; i <= stacks; i++)
    {
        float phi = glm::pi<float>() * (float)i / (float)stacks;
        for (int j = 0; j <= slices; j++)
        {
            float theta = 2.0f * glm::pi<float>() * (float)j / (float)slices;

            float x = radius * sinf(phi) * cosf(theta);
            float y = radius * cosf(phi);
            float z = radius * sinf(phi) * sinf(theta);

            Vertex v;
            v.position = glm::vec3(x, y, z);
            v.normal = glm::normalize(glm::vec3(x, y, z));
            v.color = glm::u8vec4(255, 255, 255, 255);
            v.texcoord = glm::vec2((float)j / slices, (float)i / stacks);
            vertices.push_back(v);
        }
    }

    for (int i = 0; i < stacks; i++)
    {
        for (int j = 0; j < slices; j++)
        {
            int first = i * (slices + 1) + j;
            int second = first + slices + 1;

            indices.push_back(first);
            indices.push_back(second);
            indices.push_back(first + 1);

            indices.push_back(second);
            indices.push_back(second + 1);
            indices.push_back(first + 1);
        }
    }

    mesh.meshType = GL_TRIANGLES;
    mesh.vertexCount = (int)vertices.size();
    mesh.indexCount = (int)indices.size();

    glGenVertexArrays(1, &mesh.vertexArrayObject);
    glGenBuffers(1, &mesh.vertexBufferObject);
    glGenBuffers(1, &mesh.indexBufferObject);

    glBindVertexArray(mesh.vertexArrayObject);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.vertexBufferObject);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * mesh.vertexCount, vertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)offsetof(Vertex, color));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texcoord));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.indexBufferObject);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(Uint32) * mesh.indexCount, indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// Initialize cubemap FBO for reflection rendering
void InitCubemapFBO()
{
    glGenTextures(1, &cubemapTexture);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);

    for (int i = 0; i < 6; i++)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB,
            CUBEMAP_SIZE, CUBEMAP_SIZE, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &cubemapFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, cubemapFBO);

    glGenRenderbuffers(1, &cubemapDepthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, cubemapDepthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, CUBEMAP_SIZE, CUBEMAP_SIZE);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, cubemapDepthRBO);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Render scene objects with a given shader, projection, and view matrix
// Used both for main rendering and cubemap face rendering
void RenderSceneObjects(Shader& shader, const glm::mat4& projection, const glm::mat4& view, bool skipTriangle = false)
{
    glUseProgram(shader.programId);

    shader.SetUniformMat4(shader.GetUniformLocation("projectionMatrix"), projection);
    shader.SetUniformMat4(shader.GetUniformLocation("viewMatrix"), view);

    // Set global ambient
    GLint globalAmbientLoc = shader.GetUniformLocation("globalAmbientColor");
    if (globalAmbientLoc >= 0)
        shader.SetUniformVec3(globalAmbientLoc, glm::vec3(globalAmbientColor));

    // Upload lights
    int enabledLightCount = 0;
    for (int i = 0; i < MAX_LIGHTS; i++)
        if (lights[i].enabled) enabledLightCount++;

    GLint numLightsLoc = shader.GetUniformLocation("numLights");
    if (numLightsLoc >= 0) shader.SetUniformInt(numLightsLoc, enabledLightCount);

    int lightSlot = 0;
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        const Light& light = lights[i];
        if (!light.enabled) continue;

        char uname[64];
        SDL_snprintf(uname, sizeof(uname), "lightPositions[%d]", lightSlot);
        GLint loc = shader.GetUniformLocation(uname);
        if (loc >= 0) shader.SetUniformVec4(loc, light.position);

        SDL_snprintf(uname, sizeof(uname), "lightColors[%d]", lightSlot);
        loc = shader.GetUniformLocation(uname);
        if (loc >= 0) shader.SetUniformVec4(loc, light.diffuseColor);

        SDL_snprintf(uname, sizeof(uname), "lightIntensities[%d]", lightSlot);
        loc = shader.GetUniformLocation(uname);
        if (loc >= 0) shader.SetUnifromFLoat(loc, light.intensity);

        SDL_snprintf(uname, sizeof(uname), "lightAttenuationConstants[%d]", lightSlot);
        loc = shader.GetUniformLocation(uname);
        if (loc >= 0) shader.SetUnifromFLoat(loc, light.attenuationConstant);

        SDL_snprintf(uname, sizeof(uname), "lightAttenuationLinears[%d]", lightSlot);
        loc = shader.GetUniformLocation(uname);
        if (loc >= 0) shader.SetUnifromFLoat(loc, light.attenuationLinear);

        SDL_snprintf(uname, sizeof(uname), "lightAttenuationQuadratics[%d]", lightSlot);
        loc = shader.GetUniformLocation(uname);
        if (loc >= 0) shader.SetUnifromFLoat(loc, light.attenuationQuadratic);

        lightSlot++;
    }

    // Draw mesh instances
    for (const auto& inst : meshInstances)
    {
        if (!inst.mesh || !inst.visible) continue;

        glm::mat4 model(1.f);
        model = glm::translate(model, inst.position);
        model = glm::rotate(model, glm::radians(inst.rotation.x), { 1,0,0 });
        model = glm::rotate(model, glm::radians(inst.rotation.y), { 0,1,0 });
        model = glm::rotate(model, glm::radians(inst.rotation.z), { 0,0,1 });
        model = glm::scale(model, inst.scale);

        GLint modelLoc = shader.GetUniformLocation("modelMatrix");
        if (modelLoc >= 0) shader.SetUniformMat4(modelLoc, model);

        GLint colorLoc = shader.GetUniformLocation("color");
        if (colorLoc >= 0) shader.SetUniformVec4(colorLoc, inst.color);

        GLint useTexLoc = shader.GetUniformLocation("useTexture");
        if (useTexLoc >= 0) shader.SetUniformInt(useTexLoc, inst.useTexture ? 1 : 0);

        if (inst.useTexture && inst.textureID > 0)
        {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, inst.textureID);
            GLint samplerLoc = shader.GetUniformLocation("textureSampler");
            if (samplerLoc >= 0) shader.SetUniformInt(samplerLoc, 0);
        }

        DrawMesh(*inst.mesh);

        if (inst.useTexture && inst.textureID > 0)
            glBindTexture(GL_TEXTURE_2D, 0);
    }

    glUseProgram(0);
}

// Render scene into cubemap from a world position
void RenderCubemap(const glm::vec3& position)
{
    glBindFramebuffer(GL_FRAMEBUFFER, cubemapFBO);
    glViewport(0, 0, CUBEMAP_SIZE, CUBEMAP_SIZE);

    glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.05f, 100.0f);

    // The 6 cubemap face view matrices
    glm::mat4 captureViews[6] = {
        glm::lookAt(position, position + glm::vec3( 1, 0, 0), glm::vec3(0,-1, 0)),
        glm::lookAt(position, position + glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0)),
        glm::lookAt(position, position + glm::vec3( 0, 1, 0), glm::vec3(0, 0, 1)),
        glm::lookAt(position, position + glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1)),
        glm::lookAt(position, position + glm::vec3( 0, 0, 1), glm::vec3(0,-1, 0)),
        glm::lookAt(position, position + glm::vec3( 0, 0,-1), glm::vec3(0,-1, 0)),
    };

    for (int face = 0; face < 6; face++)
    {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapTexture, 0);

        glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);

        RenderSceneObjects(basicShader, captureProjection, captureViews[face]);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Light gizmo: a small cross/star shape to mark light positions
void InitializeLightGizmo()
{
    // 3 axis lines forming a cross (6 vertices)
    float s = 0.15f;
    float verts[] = {
        -s, 0, 0,   s, 0, 0,   // X axis
         0,-s, 0,   0, s, 0,   // Y axis
         0, 0,-s,   0, 0, s,   // Z axis
    };

    glGenVertexArrays(1, &lightGizmoVAO);
    glGenBuffers(1, &lightGizmoVBO);
    glBindVertexArray(lightGizmoVAO);
    glBindBuffer(GL_ARRAY_BUFFER, lightGizmoVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void DrawLightGizmos(Shader& shader, const glm::mat4& projection, const glm::mat4& view)
{
    if (!showLightGizmos) return;

    glUseProgram(shader.programId);
    shader.SetUniformMat4(shader.GetUniformLocation("projectionMatrix"), projection);
    shader.SetUniformMat4(shader.GetUniformLocation("viewMatrix"), view);

    GLint useTextureLoc = shader.GetUniformLocation("useTexture");
    if (useTextureLoc >= 0) shader.SetUniformInt(useTextureLoc, 0);

    // Override lighting so gizmos render at full brightness
    GLint numLightsLoc = shader.GetUniformLocation("numLights");
    if (numLightsLoc >= 0) shader.SetUniformInt(numLightsLoc, 0);
    GLint ambientLoc = shader.GetUniformLocation("globalAmbientColor");
    if (ambientLoc >= 0) shader.SetUniformVec3(ambientLoc, glm::vec3(1.0f));

    // Disable depth test so gizmos always show
    glDisable(GL_DEPTH_TEST);
    glLineWidth(2.0f);

    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (!lights[i].enabled) continue;

        glm::vec3 pos = glm::vec3(lights[i].position);

        // Draw cross at light position
        glm::mat4 model = glm::translate(glm::mat4(1.0f), pos);
        shader.SetUniformMat4(shader.GetUniformLocation("modelMatrix"), model);

        // Set color to the light's diffuse color
        GLint colorLoc = shader.GetUniformLocation("color");
        if (colorLoc >= 0) shader.SetUniformVec4(colorLoc, lights[i].diffuseColor);

        // Set default vertex attribs for the gizmo (position-only VAO)
        glBindVertexArray(lightGizmoVAO);
        glDisableVertexAttribArray(1); glVertexAttrib4f(1, 1, 1, 1, 1);
        glDisableVertexAttribArray(2); glVertexAttrib2f(2, 0, 0);
        glDisableVertexAttribArray(3); glVertexAttrib3f(3, 0, 0, 1);
        glDrawArrays(GL_LINES, 0, 6);

        // Draw direction line for point lights (downward ray showing where light shines)
        if (lights[i].position.w > 0.5f)
        {
            float dirVerts[] = {
                pos.x, pos.y, pos.z,
                pos.x, pos.y - 0.5f, pos.z
            };
            GLuint tmpVAO, tmpVBO;
            glGenVertexArrays(1, &tmpVAO);
            glGenBuffers(1, &tmpVBO);
            glBindVertexArray(tmpVAO);
            glBindBuffer(GL_ARRAY_BUFFER, tmpVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(dirVerts), dirVerts, GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(0);
            glDisableVertexAttribArray(1); glVertexAttrib4f(1, 1, 1, 0, 1);
            glDisableVertexAttribArray(2); glVertexAttrib2f(2, 0, 0);
            glDisableVertexAttribArray(3); glVertexAttrib3f(3, 0, 0, 1);

            glm::mat4 identity(1.0f);
            shader.SetUniformMat4(shader.GetUniformLocation("modelMatrix"), identity);

            glm::vec4 yellowColor(1.0f, 1.0f, 0.0f, 1.0f);
            if (colorLoc >= 0) shader.SetUniformVec4(colorLoc, yellowColor);

            glDrawArrays(GL_LINES, 0, 2);
            glBindVertexArray(0);
            glDeleteBuffers(1, &tmpVBO);
            glDeleteVertexArrays(1, &tmpVAO);
        }
    }

    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    glLineWidth(1.0f);
}

// Maya-style camera
glm::vec3 camTarget = { 0.0f, 1.0f, 0.0f };
float camYaw = 0.0f;
float camPitch = 15.0f;
float camDist = 8.0f;
float fov = 75.0f;

// Mouse state
bool mouseRightDown = false;
bool mouseMiddleDown = false;
bool mouseLeftDown = false;
float lastMouseX = 0.0f, lastMouseY = 0.0f;
float mouseSensitivity = 0.25f;
float panSensitivity = 0.01f;
float zoomSensitivity = 0.5f;
float flySpeed = 0.05f;

// Selection & gizmo
int selectedInstance = -1;
bool showTranslateGizmo = true;
GLuint gizmoArrowVAO = 0, gizmoArrowVBO = 0;
int gizmoArrowVertCount = 0;

enum GizmoAxis { GIZMO_NONE = 0, GIZMO_X, GIZMO_Y, GIZMO_Z };
GizmoAxis activeGizmoAxis = GIZMO_NONE;
bool gizmoDragging = false;

bool lightEnabled = true;
bool normalize = false;

// Build arrow geometry for translate gizmo
void InitializeGizmoArrows()
{
    // Each axis: a line + a cone tip (approximated with lines)
    // X = red, Y = green, Z = blue
    float len = 1.0f;
    float tipLen = 0.15f;
    float tipW = 0.04f;

    std::vector<float> verts;

    auto addLine = [&](float x1, float y1, float z1, float x2, float y2, float z2,
                       float r, float g, float b) {
        verts.insert(verts.end(), { x1, y1, z1, r, g, b });
        verts.insert(verts.end(), { x2, y2, z2, r, g, b });
    };

    auto addConeTip = [&](int axis, float r, float g, float b) {
        // 4 lines forming a diamond tip
        float base = len - tipLen;
        for (int i = 0; i < 4; i++)
        {
            float a = (float)i * 3.14159f * 0.5f;
            float offA = tipW * cosf(a);
            float offB = tipW * sinf(a);

            float bx = 0, by = 0, bz = 0;
            float tx = 0, ty = 0, tz = 0;
            if (axis == 0) { bx = base; by = offA; bz = offB; tx = len; }
            if (axis == 1) { by = base; bx = offA; bz = offB; ty = len; }
            if (axis == 2) { bz = base; bx = offA; by = offB; tz = len; }
            addLine(bx, by, bz, tx, ty, tz, r, g, b);
        }
    };

    // X axis (red)
    addLine(0, 0, 0, len, 0, 0, 1, 0, 0);
    addConeTip(0, 1, 0, 0);
    // Y axis (green)
    addLine(0, 0, 0, 0, len, 0, 0, 1, 0);
    addConeTip(1, 0, 1, 0);
    // Z axis (blue)
    addLine(0, 0, 0, 0, 0, len, 0, 0, 1);
    addConeTip(2, 0, 0, 1);

    gizmoArrowVertCount = (int)(verts.size() / 6);

    glGenVertexArrays(1, &gizmoArrowVAO);
    glGenBuffers(1, &gizmoArrowVBO);
    glBindVertexArray(gizmoArrowVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gizmoArrowVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    // position
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // color (in attrib slot 1 as vec4, we pass rgb and set a=1)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void DrawTranslateGizmo(Shader& shader, const glm::mat4& proj, const glm::mat4& view, const glm::vec3& pos)
{
    glUseProgram(shader.programId);
    shader.SetUniformMat4(shader.GetUniformLocation("projectionMatrix"), proj);
    shader.SetUniformMat4(shader.GetUniformLocation("viewMatrix"), view);

    glm::mat4 model = glm::translate(glm::mat4(1.0f), pos);
    shader.SetUniformMat4(shader.GetUniformLocation("modelMatrix"), model);

    // Override lighting - full bright
    GLint numLightsLoc = shader.GetUniformLocation("numLights");
    if (numLightsLoc >= 0) shader.SetUniformInt(numLightsLoc, 0);
    GLint ambientLoc = shader.GetUniformLocation("globalAmbientColor");
    if (ambientLoc >= 0) shader.SetUniformVec3(ambientLoc, glm::vec3(1.0f));

    GLint useTexLoc = shader.GetUniformLocation("useTexture");
    if (useTexLoc >= 0) shader.SetUniformInt(useTexLoc, 0);
    GLint colorLoc = shader.GetUniformLocation("color");
    if (colorLoc >= 0) shader.SetUniformVec4(colorLoc, glm::vec4(1.0f));

    glDisable(GL_DEPTH_TEST);
    glLineWidth(3.0f);

    glBindVertexArray(gizmoArrowVAO);
    // Attrib 1 has RGB from our buffer, but shader expects u8vec4. We need to use the color uniform per-axis instead.
    // Disable attrib 1 and draw 3 separate axis groups with color uniform
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2); glVertexAttrib2f(2, 0, 0);
    glDisableVertexAttribArray(3); glVertexAttrib3f(3, 0, 0, 1);

    // X axis: first 10 verts (1 line + 4 cone lines = 2+8=10)
    glVertexAttrib4f(1, 1, 0, 0, 1);
    if (colorLoc >= 0) shader.SetUniformVec4(colorLoc, activeGizmoAxis == GIZMO_X ? glm::vec4(1, 1, 0, 1) : glm::vec4(1, 0, 0, 1));
    glDrawArrays(GL_LINES, 0, 10);

    // Y axis: next 10 verts
    glVertexAttrib4f(1, 0, 1, 0, 1);
    if (colorLoc >= 0) shader.SetUniformVec4(colorLoc, activeGizmoAxis == GIZMO_Y ? glm::vec4(1, 1, 0, 1) : glm::vec4(0, 1, 0, 1));
    glDrawArrays(GL_LINES, 10, 10);

    // Z axis: next 10 verts
    glVertexAttrib4f(1, 0, 0, 1, 1);
    if (colorLoc >= 0) shader.SetUniformVec4(colorLoc, activeGizmoAxis == GIZMO_Z ? glm::vec4(1, 1, 0, 1) : glm::vec4(0, 0, 1, 1));
    glDrawArrays(GL_LINES, 20, 10);

    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    glLineWidth(1.0f);
    glUseProgram(0);
}

// Helper: get camera forward/right/up from yaw/pitch
glm::vec3 getCamForward()
{
    float yawRad = glm::radians(camYaw);
    float pitchRad = glm::radians(camPitch);
    return glm::normalize(glm::vec3(
        sinf(yawRad) * cosf(pitchRad),
        -sinf(pitchRad),
        -cosf(yawRad) * cosf(pitchRad)
    ));
}

glm::vec3 getCamRight()
{
    float yawRad = glm::radians(camYaw);
    return glm::normalize(glm::vec3(cosf(yawRad), 0.0f, sinf(yawRad)));
}

glm::vec3 getCamUp()
{
    return glm::normalize(glm::cross(getCamRight(), getCamForward()));
}

glm::vec3 getCamPosition()
{
    glm::vec3 fwd = getCamForward();
    return camTarget - fwd * camDist;
}

// GUI rendering function
void GUI()
{
    ImGui::StyleColorsDark();
    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Header] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.45f, 0.45f, 0.45f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.1f, 0.1f, 0.1f, 0.5f);

    ImGui::GetStyle().FrameRounding = 0.0f;
    ImGui::GetStyle().WindowRounding = 0.0f;
    ImGui::GetStyle().FramePadding = ImVec2(4, 3);
    ImGui::GetStyle().ItemSpacing = ImVec2(8, 4);

    ImVec2 windowPos = ImVec2(10, 10);
    ImVec2 windowSize = ImVec2(320, 750);
    ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Scene Config"))
    {
        ImGui::End();
        return;
    }

    // Stats section
    if (ImGui::CollapsingHeader("Stats", ImGuiTreeNodeFlags_DefaultOpen))
    {
        Uint64 frameMs = SDL_GetTicksNS() / 1000000;
        ImGui::Text("Delta: %.3f ms", frameMs);
        ImGui::Text("Draw: %.4f ms, Frame: %.4f ms",
            (float)drawSamples.GetAverage(),
            (float)frameSamples.GetAverage());
    }

    // Scene Config section
    if (ImGui::CollapsingHeader("Scene Config", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Reload Scene button
        if (ImGui::Button("Reload Scene", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
        {
            SDL_Log("Reloading scene...");
            // Reload scene logic would go here
        }

        // Shader Controls
        if (ImGui::CollapsingHeader("Shader Controls"))
        {
            if (ImGui::Button("Reload Shaders", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
            {
                SDL_Log("Reloading shaders...");
                basicShader.Unload();
                // Use our direct shader creation instead of file loading
                basicShader.programId = createShaderProgram();
                basicShader.linkStatus = GL_TRUE;
            }

            ImGui::Checkbox("Show Triangle", &showTriangle);
        }

        // Lighting section
        if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // Global ambient color controls
            ImGui::BeginGroup();
            ImGui::PushItemWidth((ImGui::GetContentRegionAvail().x - 20) / 3);

            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.5f, 0.0f, 0.0f, 0.5f));
            ImGui::DragFloat("R", &globalAmbientColor.r, 0.01f, 0.0f, 1.0f, "%.3f");
            ImGui::PopStyleColor();
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.5f, 0.0f, 0.5f));
            ImGui::DragFloat("G", &globalAmbientColor.g, 0.01f, 0.0f, 1.0f, "%.3f");
            ImGui::PopStyleColor();
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.5f, 0.5f));
            ImGui::DragFloat("B", &globalAmbientColor.b, 0.01f, 0.0f, 1.0f, "%.3f");
            ImGui::PopStyleColor();

            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::Text("Ambient");
            ImGui::EndGroup();

            for (int i = 0; i < MAX_LIGHTS; i++)
            {
                char lightLabel[16];
                SDL_snprintf(lightLabel, sizeof(lightLabel), "Light %d", i);

                ImGui::PushID(i);
                bool isOpen = ImGui::TreeNodeEx(lightLabel,
                    ImGuiTreeNodeFlags_SpanAvailWidth |
                    (i == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0));

                if (isOpen)
                {
                    ImGui::Checkbox("Enabled", &lights[i].enabled);

                    bool lightDirectional = lights[i].position.w == 0.0f;
                    if (ImGui::Checkbox("Directional Light", &lightDirectional))
                    {
                        // When toggling between directional and point light
                        lights[i].position.w = lightDirectional ? 0.0f : 1.0f;
                    }

                    // Show position label based on light type
                    const char* posLabel = lightDirectional ? "Direction" : "Position";
                    ImGui::SliderFloat3(posLabel, &lights[i].position.x, -12.0f, 12.0f, "%.1f");

                    // Light colors
                    ImGui::ColorEdit3("Ambient Color", &lights[i].ambientColor.r, ImGuiColorEditFlags_Float);
                    ImGui::ColorEdit3("Diffuse Color", &lights[i].diffuseColor.r, ImGuiColorEditFlags_Float);
                    ImGui::ColorEdit3("Specular Color", &lights[i].specularColor.r, ImGuiColorEditFlags_Float);

                    ImGui::SliderFloat("Light Intensity", &lights[i].intensity, 0.0f, 12.0f);

                    // Only show attenuation for point lights (w == 1.0)
                    if (!lightDirectional && ImGui::TreeNode("Attenuation"))
                    {
                        ImGui::SliderFloat("Constant", &lights[i].attenuationConstant, 0.0f, 1.0f);
                        ImGui::SliderFloat("Linear", &lights[i].attenuationLinear, 0.0f, 4.0f);
                        ImGui::SliderFloat("Quadratic", &lights[i].attenuationQuadratic, 0.0f, 4.0f);
                        ImGui::TreePop();
                    }

                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }

        // Mesh Instances section
        if (ImGui::CollapsingHeader("Mesh Instances", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Show Translate Gizmo", &showTranslateGizmo);

            for (int i = 0; i < (int)meshInstances.size(); i++)
            {
                ImGui::PushID(1000 + i);
                auto& inst = meshInstances[i];

                bool isSelected = (selectedInstance == i);
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
                if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;

                bool isOpen = ImGui::TreeNodeEx(inst.name.c_str(), flags);

                // Click to select
                if (ImGui::IsItemClicked(0))
                {
                    selectedInstance = isSelected ? -1 : i;
                }

                // Quick visibility toggle on the same line
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10);
                ImGui::Checkbox("##vis", &inst.visible);

                if (isOpen)
                {
                    ImGui::DragFloat3("Position", &inst.position.x, 0.05f, -50.0f, 50.0f, "%.2f");
                    ImGui::DragFloat3("Rotation", &inst.rotation.x, 0.5f, -360.0f, 360.0f, "%.1f");
                    ImGui::DragFloat3("Scale", &inst.scale.x, 0.01f, 0.01f, 10.0f, "%.3f");
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }

        // Reflection Sphere section
        if (ImGui::CollapsingHeader("Reflection Sphere", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Show Reflection Sphere", &showReflectionSphere);
            ImGui::DragFloat3("Sphere Position", &reflSpherePos.x, 0.05f, -50.0f, 50.0f, "%.2f");
            ImGui::DragFloat("Sphere Scale", &reflSphereScale, 0.01f, 0.05f, 5.0f, "%.2f");
            ImGui::SliderFloat("Reflectivity", &reflectivity, 0.0f, 1.0f, "%.2f");
        }

        // View controls
        if (ImGui::CollapsingHeader("View Controls"))
        {
            ImGui::Checkbox("Show Light Gizmos", &showLightGizmos);
            ImGui::DragFloat3("Camera Target", &camTarget.x, 0.05f, -50.0f, 50.0f, "%.2f");
            ImGui::DragFloat("Camera Distance", &camDist, 0.1f, 0.5f, 100.0f, "%.1f");
            ImGui::DragFloat("Yaw", &camYaw, 0.5f, -360.0f, 360.0f, "%.1f");
            ImGui::DragFloat("Pitch", &camPitch, 0.5f, -89.0f, 89.0f, "%.1f");
            ImGui::DragFloat("FOV", &fov, 0.5f, 10.0f, 120.0f, "%.1f");
        }
    }

    ImGui::End();
}

// Creates a new "Shader" section in ImGui
void ShaderGUI()
{
    ImVec2 windowPos = ImVec2(10, 650);
    ImVec2 windowSize = ImVec2(300, 300);
    ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Shader"))
    {
        ImGui::End();
        return;
    }

    ImGui::Text("Program ID: %d", basicShader.programId);
    ImGui::Text("Vertex Shader:");
    ImGui::Text("Link Status: %s", (basicShader.linkStatus == GL_TRUE) ? "Success" : "Failed");

    if (ImGui::Button("Reload Shaders", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
    {
        basicShader.Unload();
        basicShader.programId = createShaderProgram();
        basicShader.linkStatus = GL_TRUE; 
    }

    ImGui::End();
}

// Texture GUI window to show loaded textures
void TextureGUI()
{
    ImVec2 windowPos = ImVec2(320, 650);
    ImVec2 windowSize = ImVec2(300, 300);
    ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Textures"))
    {
        ImGui::End();
        return;
    }

    ImGui::Text("Loaded Textures: %d", (int)textures.size());

    for (const auto& instance : meshInstances)
    {
        if (instance.useTexture && instance.textureID > 0)
        {
            ImGui::Text("%s: Texture ID %d", instance.name.c_str(), instance.textureID);
        }
    }

    ImGui::End();
}

// SDL Application Initialization
SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv)
{
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == false)
    {
        SDL_Log("SDL Init failed!");
        return SDL_APP_FAILURE;
    }

    int windowWidth = 2560;
    int windowHeight = 1440;

    // Create app context
    AppContext* app = (AppContext*)SDL_malloc(sizeof(AppContext));

    // Set up OpenGL attributes for OpenGL 3.3 Core Profile
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    // Create window with OpenGL context
    app->window = SDL_CreateWindow("ANGD 6372", windowWidth, windowHeight,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!app->window)
    {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        SDL_free(app);
        return SDL_APP_FAILURE;
    }

    // Create OpenGL context
    app->context = SDL_GL_CreateContext(app->window);
    if (!app->context)
    {
        SDL_Log("Failed to create OpenGL context: %s", SDL_GetError());
        SDL_DestroyWindow(app->window);
        SDL_free(app);
        return SDL_APP_FAILURE;
    }

    // Initialize GLEW
    glewExperimental = GL_TRUE; 
    GLenum glewError = glewInit();
    if (glewError != GLEW_OK)
    {
        SDL_Log("GLEW initialization failed: %s", glewGetErrorString(glewError));
        SDL_GL_DestroyContext(app->context);
        SDL_DestroyWindow(app->window);
        SDL_free(app);
        return SDL_APP_FAILURE;
    }

    // Check for any remaining errors after GLEW init (some drivers have an "invalid enum" error that can be ignored)
    glGetError(); 
    SDL_GL_SetSwapInterval(1);

    // Log OpenGL version
    int majorVersion, minorVersion;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &majorVersion);
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minorVersion);
    SDL_Log("Success! Started an OpenGL %d.%d context.", majorVersion, minorVersion);

    // Initialize quad mesh
    InitializeQuadMesh();

    // Initialize grid
    InitializeGrid(11, 5.0f);

    // Initialize light gizmos
    InitializeLightGizmo();

    // Initialize translate gizmo
    InitializeGizmoArrows();

    // Generate reflection sphere mesh
    GenerateSphereMesh(sphereMesh, 32, 32, 1.0f);

    // Initialize cubemap FBO
    InitCubemapFBO();

    // Get keyboard state
    keyboardState = SDL_GetKeyboardState(NULL);

    // Create shader program directly (not from files)
    basicShader.programId = createShaderProgram();
    if (basicShader.programId > 0)
    {
        basicShader.linkStatus = GL_TRUE;
        basicShader.vertStatus = GL_TRUE;
        basicShader.fragStatus = GL_TRUE;
        SDL_Log("Shader program created successfully: %d", basicShader.programId);
    }
    else
    {
        SDL_Log("Failed to create shader program!");
        basicShader.linkStatus = GL_FALSE;
    }

    // Create reflection shader (reuses basic vertex shader, new fragment)
    {
        GLuint vertShader = loadAndCompileShader(GL_VERTEX_SHADER, "assets/shaders/basicVert.glsl");
        GLuint fragShader = loadAndCompileShader(GL_FRAGMENT_SHADER, "assets/shaders/reflectionFrag.glsl");
        if (vertShader && fragShader)
        {
            reflectionShader.programId = glCreateProgram();
            glAttachShader(reflectionShader.programId, vertShader);
            glAttachShader(reflectionShader.programId, fragShader);
            glLinkProgram(reflectionShader.programId);
            GLint success;
            glGetProgramiv(reflectionShader.programId, GL_LINK_STATUS, &success);
            reflectionShader.linkStatus = success ? GL_TRUE : GL_FALSE;
            if (!success) SDL_Log("Reflection shader link failed!");
            else SDL_Log("Reflection shader created: %d", reflectionShader.programId);
        }
        if (vertShader) glDeleteShader(vertShader);
        if (fragShader) glDeleteShader(fragShader);
    }

    // Load configuration files
    {
        sol::state sceneLua;
        sceneLua.open_libraries(sol::lib::base, sol::lib::math);
        sceneLua.script_file("assets/Scripts/config.lua");

        sol::table sceneConfig = sceneLua["config"];

        if (sceneConfig.valid())
        {
            windowWidth = sceneConfig.get_or("width", windowWidth);
            windowHeight = sceneConfig.get_or("height", windowHeight);

            // Load meshes from configuration
            sol::optional<sol::table> meshesOptional = sceneConfig["meshes"];
            if (meshesOptional != sol::nullopt)
            {
                sol::table meshesTable = meshesOptional.value();

                for (const auto& item : meshesTable)
                {
                    std::string key = item.first.as<std::string>();
                    std::string value = item.second.as<std::string>();

                    SDL_Log("Loading mesh: %s from %s", key.c_str(), value.c_str());

                    Mesh* newMesh = (Mesh*)SDL_malloc(sizeof(Mesh));
                    new (newMesh) Mesh();
                    newMesh->LoadFromFile(value.c_str());
                    meshes.emplace(key, newMesh);
                }
            }

            // Load textures from configuration (if present)
            sol::optional<sol::table> texturesOptional = sceneConfig["textures"];
            if (texturesOptional != sol::nullopt)
            {
                sol::table texturesTable = texturesOptional.value();

                for (const auto& item : texturesTable)
                {
                    std::string key = item.first.as<std::string>();
                    std::string value = item.second.as<std::string>();

                    SDL_Log("Loading texture: %s from %s", key.c_str(), value.c_str());
                    GLuint textureID = loadTexture(value.c_str());
                    if (textureID > 0)
                    {
                        textures.emplace(key, textureID);
                    }
                }
            }
        }
    }

    // Initialize ImGui
    imguiContext = ImGui::CreateContext();
    ImGui_ImplSDL3_InitForOpenGL(app->window, app->context);

    // This needs to match your OpenGL version
    ImGui_ImplOpenGL3_Init("#version 330 core");

    // Enable first light by default
    lights[0].enabled = true;

    // Load scene configuration
    {
        sol::state sceneLua;
        sceneLua.open_libraries(sol::lib::base, sol::lib::math);
        sceneLua.script_file("assets/Scripts/scene.lua");

        sol::table sceneConfig = sceneLua["scene"];
        if (sceneConfig.valid())
        {
            // Load mesh instances
            sol::table configMeshInstances = sceneConfig["mesh_instances"];
            if (configMeshInstances.valid())
            {
                for (int i = 1; i <= configMeshInstances.size(); ++i)
                {
                    sol::table instance = configMeshInstances[i];
                    if (instance.valid())
                    {
                        MeshInstance newInstance;

                        newInstance.name = instance["name"];
                        newInstance.position = vec3FromTable(instance["position"]);
                        newInstance.rotation = vec3FromTable(instance["rotation"]);
                        newInstance.scale = vec3FromTable(instance["scale"]);

                        // Load color if present
                        sol::optional<sol::table> colorTable = instance["color"];
                        if (colorTable != sol::nullopt)
                        {
                            newInstance.color = vec4FromTable(colorTable);
                        }

                        // Load texture if present
                        std::string textureName = instance.get_or<std::string>("texture", "");
                        if (!textureName.empty() && textures.find(textureName) != textures.end())
                        {
                            newInstance.textureID = textures[textureName];
                            newInstance.useTexture = true;
                            SDL_Log("Assigned texture '%s' to instance '%s'",
                                textureName.c_str(), newInstance.name.c_str());
                        }
                        // If no texture in config, check for texture path
                        else
                        {
                            std::string texturePath = instance.get_or<std::string>("texturePath", "");
                            if (!texturePath.empty())
                            {
                                newInstance.textureID = loadTexture(texturePath.c_str());
                                newInstance.useTexture = newInstance.textureID > 0;
                                SDL_Log("Loaded texture from path '%s' for instance '%s'",
                                    texturePath.c_str(), newInstance.name.c_str());
                            }
                        }

                        std::string meshName = instance.get_or<std::string>("mesh", "");
                        if (!meshName.empty() && meshes.find(meshName) != meshes.end())
                        {
                            newInstance.mesh = meshes[meshName];
                        }
                        else
                        {
                            SDL_Log("Mesh '%s' not found, using quad", meshName.c_str());
                            newInstance.mesh = &quadMesh;
                        }

                        meshInstances.emplace_back(newInstance);
                    }
                }
            }

            // Load global ambient color
            globalAmbientColor = vec4FromTable(sceneConfig["global_ambient"]);

            // Load lights configuration
            sol::optional<sol::table> lightsTableOpt = sceneConfig["lights"];
            if (lightsTableOpt != sol::nullopt)
            {
                sol::table lightsTable = lightsTableOpt.value();
                for (int i = 1; i <= MAX_LIGHTS && i <= lightsTable.size(); ++i)
                {
                    sol::table lightConfig = lightsTable[i];
                    if (!lightConfig.valid()) continue;

                    Light& light = lights[i - 1];

                    light.enabled = lightConfig.get_or("enabled", false);
                    light.position = vec4FromTable(lightConfig["position"]);
                    light.ambientColor = vec4FromTable(lightConfig["ambient"]);
                    light.diffuseColor = vec4FromTable(lightConfig["diffuse"]);
                    light.specularColor = vec4FromTable(lightConfig["specular"]);
                    light.intensity = lightConfig.get_or("intensity", 1.0f);
                    light.attenuationConstant = lightConfig.get_or("atten_const", 1.0f);
                    light.attenuationLinear = lightConfig.get_or("atten_linear", 0.0f);
                    light.attenuationQuadratic = lightConfig.get_or("atten_quad", 0.0f);
                }
            }
        }
    }

    // Yellow triangle setup with VAO for modern OpenGL
    float triangleVertices[] = {
        -0.5f, -0.5f, 0.0f,  // bottom left
         0.5f, -0.5f, 0.0f,  // bottom right
         0.0f,  0.5f, 0.0f   // top center
    };

    glGenVertexArrays(1, &triangleVAO);
    glGenBuffers(1, &triangleVBO);

    glBindVertexArray(triangleVAO);

    glBindBuffer(GL_ARRAY_BUFFER, triangleVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangleVertices), triangleVertices, GL_STATIC_DRAW);

    // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    LogLastGLError();

    *appstate = app;
    return SDL_APP_CONTINUE;
}

// Replace your existing SDL_AppIterate function with this version
SDL_AppResult SDL_AppIterate(void* appstate)
{
    const Uint64 frameStart = SDL_GetTicksNS();
    auto* app = static_cast<AppContext*>(appstate);

    // Reset per-frame scene counters
    frameDrawCalls = 0;
    frameTriangles = 0;

    // Lazily create the GPU timer queries on first frame
    if (gpuQueries[0] == 0)
    {
        glGenQueries(2, gpuQueries);
    }

    // ── ImGui frame ───────────────────────────────────────────────────────────
    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    GUI();
    ShaderGUI();
    TextureGUI();

    // ── Maya camera: RMB+WASD fly, MMB pan, scroll zoom, Alt+LMB orbit ────
    if (!keyboardState) keyboardState = SDL_GetKeyboardState(nullptr);
    SDL_PumpEvents();

    if (!ImGui::GetIO().WantCaptureKeyboard && mouseRightDown)
    {
        glm::vec3 fwd = getCamForward();
        glm::vec3 right = getCamRight();
        glm::vec3 up(0, 1, 0);

        if (keyboardState[SDL_SCANCODE_W]) camTarget += fwd * flySpeed;
        if (keyboardState[SDL_SCANCODE_S]) camTarget -= fwd * flySpeed;
        if (keyboardState[SDL_SCANCODE_A]) camTarget -= right * flySpeed;
        if (keyboardState[SDL_SCANCODE_D]) camTarget += right * flySpeed;
        if (keyboardState[SDL_SCANCODE_E]) camTarget += up * flySpeed;
        if (keyboardState[SDL_SCANCODE_Q]) camTarget -= up * flySpeed;
    }

    // ── Camera matrices ──────────────────────────────────────────────────────
    int rw, rh; SDL_GetWindowSizeInPixels(app->window, &rw, &rh);
    const float aspect = rh ? static_cast<float>(rw) / rh : 1.0f;

    const glm::mat4 projection =
        glm::perspective(glm::radians(fov), aspect, 0.05f, 100.0f);

    glm::vec3 camPos = getCamPosition();
    const glm::mat4 view = glm::lookAt(camPos, camTarget, glm::vec3(0, 1, 0));

    // ── Clear ────────────────────────────────────────────────────────────────
    glViewport(0, 0, rw, rh);
    glClearColor(0.5f, 0.5f, 0.5f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    const Uint64 drawStart = SDL_GetTicksNS();

    // Begin GPU timing for this frame's scene rendering
    glBeginQuery(GL_TIME_ELAPSED, gpuQueries[gpuQueryIndex]);

    // ── Render cubemap for reflection sphere ──────────────────────────────────
    if (showReflectionSphere && reflectionShader.programId > 0)
    {
        RenderCubemap(reflSpherePos);
        // Restore viewport after cubemap rendering
        glViewport(0, 0, rw, rh);
    }

    // ── Render scene normally ─────────────────────────────────────────────────
    RenderSceneObjects(basicShader, projection, view);

    // ── Render reflection sphere ──────────────────────────────────────────────
    if (showReflectionSphere && reflectionShader.programId > 0)
    {
        glUseProgram(reflectionShader.programId);

        reflectionShader.SetUniformMat4(reflectionShader.GetUniformLocation("projectionMatrix"), projection);
        reflectionShader.SetUniformMat4(reflectionShader.GetUniformLocation("viewMatrix"), view);

        glm::mat4 sphereModel = glm::translate(glm::mat4(1.0f), reflSpherePos);
        sphereModel = glm::scale(sphereModel, glm::vec3(reflSphereScale));
        reflectionShader.SetUniformMat4(reflectionShader.GetUniformLocation("modelMatrix"), sphereModel);
        reflectionShader.SetUniformVec4(reflectionShader.GetUniformLocation("color"), glm::vec4(1.0f));

        // Camera position for reflection calculation
        reflectionShader.SetUniformVec3(reflectionShader.GetUniformLocation("cameraPosition"), camPos);
        reflectionShader.SetUnifromFLoat(reflectionShader.GetUniformLocation("reflectivity"), reflectivity);

        // Upload lighting to reflection shader too
        reflectionShader.SetUniformVec3(reflectionShader.GetUniformLocation("globalAmbientColor"), glm::vec3(globalAmbientColor));
        int eLightCount = 0;
        for (int i = 0; i < MAX_LIGHTS; i++) if (lights[i].enabled) eLightCount++;
        reflectionShader.SetUniformInt(reflectionShader.GetUniformLocation("numLights"), eLightCount);

        int slot = 0;
        for (int i = 0; i < MAX_LIGHTS; i++)
        {
            if (!lights[i].enabled) continue;
            char u[64];
            SDL_snprintf(u, sizeof(u), "lightPositions[%d]", slot);
            reflectionShader.SetUniformVec4(reflectionShader.GetUniformLocation(u), lights[i].position);
            SDL_snprintf(u, sizeof(u), "lightColors[%d]", slot);
            reflectionShader.SetUniformVec4(reflectionShader.GetUniformLocation(u), lights[i].diffuseColor);
            SDL_snprintf(u, sizeof(u), "lightIntensities[%d]", slot);
            reflectionShader.SetUnifromFLoat(reflectionShader.GetUniformLocation(u), lights[i].intensity);
            SDL_snprintf(u, sizeof(u), "lightAttenuationConstants[%d]", slot);
            reflectionShader.SetUnifromFLoat(reflectionShader.GetUniformLocation(u), lights[i].attenuationConstant);
            SDL_snprintf(u, sizeof(u), "lightAttenuationLinears[%d]", slot);
            reflectionShader.SetUnifromFLoat(reflectionShader.GetUniformLocation(u), lights[i].attenuationLinear);
            SDL_snprintf(u, sizeof(u), "lightAttenuationQuadratics[%d]", slot);
            reflectionShader.SetUnifromFLoat(reflectionShader.GetUniformLocation(u), lights[i].attenuationQuadratic);
            slot++;
        }

        // Bind cubemap
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
        reflectionShader.SetUniformInt(reflectionShader.GetUniformLocation("envMap"), 0);

        DrawMesh(sphereMesh);

        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        glUseProgram(0);
    }

    // ── Light gizmos ────────────────────────────────────────────────────────
    DrawLightGizmos(basicShader, projection, view);
    glUseProgram(0);

    // ── Translate gizmo for selected instance ────────────────────────────────
    if (showTranslateGizmo && selectedInstance >= 0 && selectedInstance < (int)meshInstances.size())
    {
        DrawTranslateGizmo(basicShader, projection, view, meshInstances[selectedInstance].position);
    }

    // End GPU timing for the scene (before ImGui, so we measure our renderer)
    glEndQuery(GL_TIME_ELAPSED);

    // Read back the OTHER query, which the GPU finished at least a frame ago.
    // Reading the query we just issued would block until the GPU drained.
    if (gpuQueriesReady)
    {
        const int readIndex = 1 - gpuQueryIndex;
        GLint available = 0;
        glGetQueryObjectiv(gpuQueries[readIndex], GL_QUERY_RESULT_AVAILABLE, &available);
        if (available)
        {
            GLuint64 gpuNs = 0;
            glGetQueryObjectui64v(gpuQueries[readIndex], GL_QUERY_RESULT, &gpuNs);
            gpuSamples.AddSample(gpuNs / 1'000'000.0);
            if (gpuValidSamples < 200) ++gpuValidSamples;
        }
    }
    gpuQueryIndex = 1 - gpuQueryIndex;
    gpuQueriesReady = true;

    const double drawMs =
        (SDL_GetTicksNS() - drawStart) / 1'000'000.0;
    drawSamples.AddSample(drawMs);

    // ── ImGui & swap ─────────────────────────────────────────────────────────
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    SDL_GL_SwapWindow(app->window);

    const double frameMs =
        (SDL_GetTicksNS() - frameStart) / 1'000'000.0;
    frameSamples.AddSample(frameMs);

    const double avgFrame = frameSamples.GetAverage();
    char title[224];
    SDL_snprintf(title, sizeof(title),
        "ANGD 6372 GL  |  Frame %.3f ms (%.1f fps)  |  CPU Draw %.3f ms  |  GPU %.3f ms  |  %ux%u  |  %u draws  |  %u tris",
        avgFrame,
        avgFrame > 0.0 ? 1000.0 / avgFrame : 0.0,
        drawSamples.GetAverage(),
        GpuAverageMs(),
        (unsigned)rw, (unsigned)rh,
        frameDrawCalls,
        frameTriangles);
    SDL_SetWindowTitle(app->window, title);

    LogLastGLError();
    return SDL_APP_CONTINUE;
}

// SDL Event Handling
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    AppContext* app = (AppContext*)appstate;

    // Process ImGui events
    ImGui_ImplSDL3_ProcessEvent(event);

    // Don't process mouse for camera/gizmo when ImGui wants it
    bool imguiWantsMouse = ImGui::GetIO().WantCaptureMouse;

    switch (event->type)
    {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;

    case SDL_EVENT_KEY_DOWN:
        if (event->key.key == SDLK_ESCAPE)
        {
            return SDL_APP_SUCCESS;
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (!imguiWantsMouse)
        {
            if (event->button.button == SDL_BUTTON_RIGHT)
            {
                mouseRightDown = true;
                lastMouseX = event->button.x;
                lastMouseY = event->button.y;
            }
            if (event->button.button == SDL_BUTTON_MIDDLE)
            {
                mouseMiddleDown = true;
                lastMouseX = event->button.x;
                lastMouseY = event->button.y;
            }
            if (event->button.button == SDL_BUTTON_LEFT)
            {
                mouseLeftDown = true;
                lastMouseX = event->button.x;
                lastMouseY = event->button.y;

                // Check if we should start gizmo drag
                if (selectedInstance >= 0 && selectedInstance < (int)meshInstances.size() && showTranslateGizmo)
                {
                    // Simple axis selection: project each axis endpoint to screen and pick closest
                    int rw, rh;
                    SDL_GetWindowSizeInPixels(app->window, &rw, &rh);
                    float aspect = rh ? (float)rw / rh : 1.0f;
                    glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, 0.05f, 100.0f);
                    glm::vec3 cPos = getCamPosition();
                    glm::mat4 vw = glm::lookAt(cPos, camTarget, glm::vec3(0, 1, 0));
                    glm::mat4 pvw = proj * vw;

                    glm::vec3 objPos = meshInstances[selectedInstance].position;
                    glm::vec4 axes[3] = {
                        glm::vec4(1, 0, 0, 0), glm::vec4(0, 1, 0, 0), glm::vec4(0, 0, 1, 0)
                    };

                    float mx = event->button.x;
                    float my = event->button.y;
                    activeGizmoAxis = GIZMO_NONE;
                    float bestDist = 30.0f; // pixel threshold

                    for (int a = 0; a < 3; a++)
                    {
                        // Project midpoint of axis line to screen
                        glm::vec3 axisEnd = objPos + glm::vec3(axes[a]) * 0.5f;
                        glm::vec4 clip = pvw * glm::vec4(axisEnd, 1.0f);
                        if (clip.w <= 0) continue;
                        glm::vec2 screen = glm::vec2(
                            (clip.x / clip.w * 0.5f + 0.5f) * rw,
                            (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * rh
                        );

                        float d = glm::length(glm::vec2(mx, my) - screen);
                        if (d < bestDist)
                        {
                            bestDist = d;
                            activeGizmoAxis = (GizmoAxis)(a + 1);
                        }
                    }

                    if (activeGizmoAxis != GIZMO_NONE)
                        gizmoDragging = true;
                }
            }
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event->button.button == SDL_BUTTON_RIGHT) mouseRightDown = false;
        if (event->button.button == SDL_BUTTON_MIDDLE) mouseMiddleDown = false;
        if (event->button.button == SDL_BUTTON_LEFT)
        {
            mouseLeftDown = false;
            gizmoDragging = false;
            activeGizmoAxis = GIZMO_NONE;
        }
        break;

    case SDL_EVENT_MOUSE_MOTION:
        if (!imguiWantsMouse)
        {
            float dx = event->motion.xrel;
            float dy = event->motion.yrel;

            bool altDown = (SDL_GetModState() & SDL_KMOD_ALT) != 0;

            // Alt + LMB = Orbit (Maya style)
            if (mouseLeftDown && altDown && !gizmoDragging)
            {
                camYaw += dx * mouseSensitivity;
                camPitch += dy * mouseSensitivity;
                camPitch = glm::clamp(camPitch, -89.0f, 89.0f);
            }
            // RMB drag (without WASD) = Orbit
            else if (mouseRightDown && !keyboardState[SDL_SCANCODE_W] &&
                     !keyboardState[SDL_SCANCODE_A] && !keyboardState[SDL_SCANCODE_S] &&
                     !keyboardState[SDL_SCANCODE_D])
            {
                camYaw += dx * mouseSensitivity;
                camPitch += dy * mouseSensitivity;
                camPitch = glm::clamp(camPitch, -89.0f, 89.0f);
            }
            // MMB = Pan
            else if (mouseMiddleDown)
            {
                glm::vec3 right = getCamRight();
                glm::vec3 up = getCamUp();
                camTarget -= right * dx * panSensitivity;
                camTarget += up * dy * panSensitivity;
            }
            // LMB + dragging gizmo = translate selected object
            else if (gizmoDragging && selectedInstance >= 0 && selectedInstance < (int)meshInstances.size())
            {
                float speed = 0.01f;
                glm::vec3& pos = meshInstances[selectedInstance].position;
                if (activeGizmoAxis == GIZMO_X) pos.x += dx * speed;
                if (activeGizmoAxis == GIZMO_Y) pos.y -= dy * speed;
                if (activeGizmoAxis == GIZMO_Z) pos.z += dx * speed;
            }
        }
        break;

    case SDL_EVENT_MOUSE_WHEEL:
        if (!imguiWantsMouse)
        {
            camDist -= event->wheel.y * zoomSensitivity;
            camDist = glm::clamp(camDist, 0.5f, 100.0f);
        }
        break;
    }

    return SDL_APP_CONTINUE;
}

// SDL Application Cleanup
void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    AppContext* app = (AppContext*)appstate;

    // Shutdown ImGui
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(imguiContext);

    // Clean up textures
    for (auto& texturePair : textures)
    {
        glDeleteTextures(1, &texturePair.second);
    }
    textures.clear();

    // Clean up meshes
    for (auto& mesh : meshes)
    {
        mesh.second->Unload();
        SDL_free(mesh.second);
    }
    meshes.clear();

    // Clean up quad mesh
    quadMesh.Unload();

    // Clean up triangle resources
    glDeleteVertexArrays(1, &triangleVAO);
    glDeleteBuffers(1, &triangleVBO);

    // Clean up grid resources
    glDeleteVertexArrays(1, &gridVAO);
    glDeleteBuffers(1, &gridVBO);

    // Clean up light gizmo
    glDeleteVertexArrays(1, &lightGizmoVAO);
    glDeleteBuffers(1, &lightGizmoVBO);

    // Clean up translate gizmo
    glDeleteVertexArrays(1, &gizmoArrowVAO);
    glDeleteBuffers(1, &gizmoArrowVBO);

    // Clean up reflection sphere resources
    sphereMesh.Unload();
    if (cubemapFBO) glDeleteFramebuffers(1, &cubemapFBO);
    if (cubemapDepthRBO) glDeleteRenderbuffers(1, &cubemapDepthRBO);
    if (cubemapTexture) glDeleteTextures(1, &cubemapTexture);

    // Unload shaders
    basicShader.Unload();
    reflectionShader.Unload();

    // Destroy OpenGL context and window
    SDL_GL_DestroyContext(app->context);
    SDL_DestroyWindow(app->window);
    SDL_free(app);

    SDL_Quit();
}