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

// Shader management
Shader basicShader;

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

// Function to draw a mesh using modern OpenGL
void DrawMesh(const Mesh& mesh)
{
    if (mesh.vertexArrayObject == 0 || mesh.indexBufferObject == 0)
        return;

    glBindVertexArray(mesh.vertexArrayObject);
    glDrawElements(mesh.meshType, mesh.indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
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
    0, 1, 2, 3
};

Mesh quadMesh;

// Initialize quad mesh with VAO
void InitializeQuadMesh()
{
    quadMesh.meshType = GL_QUADS;
    quadMesh.vertexCount = 4;
    quadMesh.indexCount = 4;

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
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
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

// Scene rendering variables
glm::vec3 modelPosition = { 0.0f, 0.0f, 0.0f };
glm::vec3 modelRotation = { 0.0f, 0.0f, 0.0f };
float camRotX = 15.0f;
float camRotY = 10.0f;
float fov = 60.0f;
float camRotZ = 10.0f;

bool lightEnabled = true;
bool normalize = false;

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
    ImVec2 windowSize = ImVec2(300, 600);
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

        // Model controls
        if (ImGui::CollapsingHeader("Model Controls"))
        {
            if (ImGui::Checkbox("Normalize", &normalize))
            {
                if (normalize)
                {
                    glDisable(GL_RESCALE_NORMAL);
                    glEnable(GL_NORMALIZE);
                }
                else
                {
                    glDisable(GL_NORMALIZE);
                    glEnable(GL_RESCALE_NORMAL);
                }
            }

            ImGui::SliderFloat3("Position", &modelPosition.x, -10.0f, 10.0f, "%.1f");
            ImGui::SliderFloat3("Rotation", &modelRotation.x, -180.0f, 180.0f, "%.1f");
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

    // ── ImGui frame ───────────────────────────────────────────────────────────
    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    GUI();
    ShaderGUI();
    TextureGUI();

    // ── Keyboard input (unchanged) ────────────────────────────────────────────
    if (!keyboardState) keyboardState = SDL_GetKeyboardState(nullptr);
    SDL_PumpEvents();

    if (!ImGui::GetIO().WantCaptureKeyboard)
    {
        if (keyboardState[SDL_SCANCODE_UP])    modelPosition.y += 0.03f;
        if (keyboardState[SDL_SCANCODE_DOWN])  modelPosition.y -= 0.03f;
        if (keyboardState[SDL_SCANCODE_LEFT])  modelPosition.x -= 0.03f;
        if (keyboardState[SDL_SCANCODE_RIGHT]) modelPosition.x += 0.03f;
        if (keyboardState[SDL_SCANCODE_Z])     modelPosition.z += 0.03f;
        if (keyboardState[SDL_SCANCODE_C])     modelPosition.z -= 0.03f;
        if (keyboardState[SDL_SCANCODE_W])     camRotX += 0.07f;
        if (keyboardState[SDL_SCANCODE_S])     camRotX -= 0.07f;
        if (keyboardState[SDL_SCANCODE_A])     camRotY += 0.07f;
        if (keyboardState[SDL_SCANCODE_D])     camRotY -= 0.07f;
    }
    if (keyboardState[SDL_SCANCODE_Q]) fov = std::max(10.0f, fov - 0.5f);
    if (keyboardState[SDL_SCANCODE_E]) fov = std::min(120.0f, fov + 0.5f);

    // ── Camera matrices ──────────────────────────────────────────────────────
    int rw, rh; SDL_GetWindowSizeInPixels(app->window, &rw, &rh);
    const float aspect = rh ? static_cast<float>(rw) / rh : 1.0f;

    const glm::mat4 projection =
        glm::perspective(glm::radians(fov), aspect, 0.05f, 100.0f);

    glm::mat4 view(1.0f);
    view = glm::translate(view, { 0.0f, -0.8f, -7.0f });
    view = glm::rotate(view, glm::radians(camRotX), { 1,0,0 });
    view = glm::rotate(view, glm::radians(camRotY), { 0,1,0 });

    // ── Clear ────────────────────────────────────────────────────────────────
    glViewport(0, 0, rw, rh);
    glClearColor(0.5f, 0.5f, 0.5f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    // ── Use the shader and prepare lighting ────────────────────────────────────────
    glUseProgram(basicShader.programId);

    // Set matrix uniforms
    GLint projectionMatrixLoc = basicShader.GetUniformLocation("projectionMatrix");
    if (projectionMatrixLoc >= 0)
    {
        basicShader.SetUniformMat4(projectionMatrixLoc, projection);
    }

    GLint viewMatrixLoc = basicShader.GetUniformLocation("viewMatrix");
    if (viewMatrixLoc >= 0)
    {
        basicShader.SetUniformMat4(viewMatrixLoc, view);
    }

    // Set global ambient
    GLint globalAmbientLoc = basicShader.GetUniformLocation("globalAmbientColor");
    if (globalAmbientLoc >= 0)
    {
        basicShader.SetUniformVec3(globalAmbientLoc, glm::vec3(globalAmbientColor));
    }

    // Count enabled lights
    int enabledLightCount = 0;
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (lights[i].enabled)
        {
            enabledLightCount++;
        }
    }

    // Set num lights uniform
    GLint numLightsLoc = basicShader.GetUniformLocation("numLights");
    if (numLightsLoc >= 0)
    {
        basicShader.SetUniformInt(numLightsLoc, enabledLightCount);
    }

    // Upload light data

    //uniform buffers
    //  // ── Look up uniform buffers ─────────────────────────────────────────────────────
    //cache the locations ----- 
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        const Light& light = lights[i];

        char uniformName[64];
        SDL_snprintf(uniformName, sizeof(uniformName), "lightPositions[%d]", i);
        GLint lightPosLoc = basicShader.GetUniformLocation(uniformName);
        if (lightPosLoc >= 0)
        {
            basicShader.SetUniformVec4(lightPosLoc, light.position);
        }

        SDL_snprintf(uniformName, sizeof(uniformName), "lightColors[%d]", i);
        GLint lightColorLoc = basicShader.GetUniformLocation(uniformName);
        if (lightColorLoc >= 0)
        {
            basicShader.SetUniformVec4(lightColorLoc, light.diffuseColor);
        }

        SDL_snprintf(uniformName, sizeof(uniformName), "lightIntensities[%d]", i);
        GLint lightIntensityLoc = basicShader.GetUniformLocation(uniformName);
        if (lightIntensityLoc >= 0)
        {
            basicShader.SetUnifromFLoat(lightIntensityLoc, light.enabled ? light.intensity : 0.0f);
        }

        SDL_snprintf(uniformName, sizeof(uniformName), "lightAttenuationConstants[%d]", i);
        GLint attConstLoc = basicShader.GetUniformLocation(uniformName);
        if (attConstLoc >= 0)
        {
            basicShader.SetUnifromFLoat(attConstLoc, light.attenuationConstant);
        }

        SDL_snprintf(uniformName, sizeof(uniformName), "lightAttenuationLinears[%d]", i);
        GLint attLinearLoc = basicShader.GetUniformLocation(uniformName);
        if (attLinearLoc >= 0)
        {
            basicShader.SetUnifromFLoat(attLinearLoc, light.attenuationLinear);
        }

        SDL_snprintf(uniformName, sizeof(uniformName), "lightAttenuationQuadratics[%d]", i);
        GLint attQuadLoc = basicShader.GetUniformLocation(uniformName);
        if (attQuadLoc >= 0)
        {
            basicShader.SetUnifromFLoat(attQuadLoc, light.attenuationQuadratic);
        }
    }

    const Uint64 drawStart = SDL_GetTicksNS();

    // ── Triangle rendering ─────────────────────────────────────────────────────
    if (showTriangle)
    {
        glm::mat4 triModel = glm::translate(glm::mat4(1.f), { 0,0,0.5f });

        // Set the model matrix for the triangle
        GLint modelMatrixLoc = basicShader.GetUniformLocation("modelMatrix");
        if (modelMatrixLoc >= 0)
        {
            basicShader.SetUniformMat4(modelMatrixLoc, triModel);
        }

        // Set color uniform
        GLint colorLoc = basicShader.GetUniformLocation("color");
        if (colorLoc >= 0)
        {
            glm::vec4 yellowColor(1.0f, 1.0f, 0.0f, 1.0f);
            basicShader.SetUniformVec4(colorLoc, yellowColor);
        }

        // Set useTexture uniform
        GLint useTextureLoc = basicShader.GetUniformLocation("useTexture");
        if (useTextureLoc >= 0)
        {
            basicShader.SetUniformInt(useTextureLoc, 0); // No texture
        }

        glBindVertexArray(triangleVAO);

        // supply default attributes so shader colour ≠ 0
        glDisableVertexAttribArray(1);  glVertexAttrib4f(1, 1, 1, 1, 1);
        glDisableVertexAttribArray(2);  glVertexAttrib2f(2, 0, 0);
        glDisableVertexAttribArray(3);  glVertexAttrib3f(3, 0, 0, 1);

        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
    }

    // ── Mesh instances ───────────────────────────────────────────────────────
    for (const auto& inst : meshInstances)
    {
        if (!inst.mesh) continue;

        // Create model matrix for this instance
        glm::mat4 model(1.f);
        model = glm::translate(model, inst.position);
        model = glm::rotate(model, glm::radians(inst.rotation.x), { 1,0,0 });
        model = glm::rotate(model, glm::radians(inst.rotation.y), { 0,1,0 });
        model = glm::rotate(model, glm::radians(inst.rotation.z), { 0,0,1 });
        model = glm::scale(model, inst.scale);

        // Set the model matrix
        GLint modelMatrixLoc = basicShader.GetUniformLocation("modelMatrix");
        if (modelMatrixLoc >= 0)
        {
            basicShader.SetUniformMat4(modelMatrixLoc, model);
        }

        // Set color uniform
        GLint colorLoc = basicShader.GetUniformLocation("color");
        if (colorLoc >= 0)
        {
            basicShader.SetUniformVec4(colorLoc, inst.color);
        }

        // Set texture uniforms
        GLint useTextureLoc = basicShader.GetUniformLocation("useTexture");
        if (useTextureLoc >= 0)
        {
            basicShader.SetUniformInt(useTextureLoc, inst.useTexture ? 1 : 0);
        }

        if (inst.useTexture && inst.textureID > 0)
        {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, inst.textureID);

            GLint samplerLoc = basicShader.GetUniformLocation("textureSampler");
            if (samplerLoc >= 0)
            {
                basicShader.SetUniformInt(samplerLoc, 0);
            }
        }

        // Draw the mesh
        DrawMesh(*inst.mesh);

        // Unbind texture if used
        if (inst.useTexture && inst.textureID > 0)
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    // Unbind the shader
    glUseProgram(0);

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

    char title[96];
    SDL_snprintf(title, sizeof(title),
        "ANGD 6372 GL  |  Draw %.3f ms  |  Frame %.3f ms",
        drawSamples.GetAverage(), frameSamples.GetAverage());
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

    // Unload shader
    basicShader.Unload();

    // Destroy OpenGL context and window
    SDL_GL_DestroyContext(app->context);
    SDL_DestroyWindow(app->window);
    SDL_free(app);

    SDL_Quit();
}