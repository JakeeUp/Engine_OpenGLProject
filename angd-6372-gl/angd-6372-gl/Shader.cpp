#include "Shader.h"

#include <glm/gtc/type_ptr.hpp>

GLint Shader::GetUniformLocation(const char* name)
{
    if (programId == 0 || linkStatus == GL_FALSE)
    {
        return -1;
    }

    GLint location = glGetUniformLocation(programId, name);
    return location;
}

void Shader::SetUnifromFLoat(const GLint id, const float value)
{
    if (programId == 0 || linkStatus == GL_FALSE)
    {
        return;
    }

    glUniform1f(id, value);
}

void Shader::SetUniformInt(const GLint id, const int value)
{
    if (programId == 0 || linkStatus == GL_FALSE) return;
    glUniform1i(id, value);
}


void Shader::SetUniformVec4(const GLint id, const glm::vec4& value)
{
    if (programId == 0 || linkStatus == GL_FALSE)
    {
        return;
    }

    glUniform4fv(id, 1, glm::value_ptr(value));
}

void Shader::SetUniformVec3(const GLint id, const glm::vec3& value)
{
    if (programId == 0 || linkStatus == GL_FALSE)
    {
        return;
    }

    glUniform3fv(id, 1, glm::value_ptr(value));
}

void Shader::SetUniformVec2(const GLint id, const glm::vec2& value)
{
    if (programId == 0 || linkStatus == GL_FALSE)
    {
        return;
    }

    glUniform2fv(id, 1, glm::value_ptr(value));
}

void Shader::SetUniformMat4(const GLint id, const glm::mat4& value)
{
    if (programId == 0 || linkStatus == GL_FALSE)
    {
        return;
    }

    glUniformMatrix4fv(id, 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::LoadShaderProgram(const char* vertShaderPath, const char* fragShaderPath)
{
    GLint vertStatus = GL_TRUE;
    std::string vertShaderInfo = "";
    GLint fragStatus = GL_TRUE;
    std::string fragShaderInfo = "";
    std::string linkInfo = "";
    GLint linkStatus = GL_TRUE;

    char* vertSource = (char*)SDL_LoadFile(vertShaderPath, nullptr);
    char* fragSource = (char*)SDL_LoadFile(fragShaderPath, nullptr);

    if (vertSource == nullptr || fragSource == nullptr)
    {
        vertStatus = GL_FALSE;
        vertShaderInfo = vertSource == nullptr ? "File not found" : "";
        fragStatus = GL_FALSE;
        fragShaderInfo = fragSource == nullptr ? "File not found" : "";
        linkStatus = GL_FALSE;
        linkInfo = "";
        SDL_free(vertSource);
        SDL_free(fragSource);
        return;
    }

    Unload();


    programId = glCreateProgram();

    // Create vertex shader
    GLuint vertShaderId = 0;
    vertShaderId = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertShaderId, 1, &vertSource, 0);
    glCompileShader(vertShaderId);

    {
        glGetShaderiv(vertShaderId, GL_COMPILE_STATUS, &vertStatus);
        int infoLength = 0;
        int maxLength = 0;
        glGetShaderiv(vertShaderId, GL_INFO_LOG_LENGTH, &maxLength);
        char* infoLog = (char*)SDL_malloc(sizeof(char) * maxLength);
        glGetShaderInfoLog(vertShaderId, maxLength, &infoLength, infoLog);

        if (infoLength > 0)
        {
            SDL_Log("Vert: %s", infoLog);
        }
        SDL_free(infoLog);
    }

    // Create fragment shader
    GLuint fragShaderId = 0;
    fragShaderId = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragShaderId, 1, &fragSource, 0);
    glCompileShader(fragShaderId);

    {
        glGetShaderiv(fragShaderId, GL_COMPILE_STATUS, &fragStatus);
        int infoLength = 0;
        int maxLength = 0;
        glGetShaderiv(fragShaderId, GL_INFO_LOG_LENGTH, &maxLength);
        char* infoLog = (char*)SDL_malloc(sizeof(char) * maxLength);
        glGetShaderInfoLog(fragShaderId, maxLength, &infoLength, infoLog);

        if (infoLength > 0)
        {
            SDL_Log("Frag: %s", infoLog);
            SDL_free(infoLog);
        }
        SDL_free(infoLog);
    }

    glAttachShader(programId, vertShaderId);
    glAttachShader(programId, fragShaderId);

    // Link
    glLinkProgram(programId);
    glGetProgramiv(programId, GL_LINK_STATUS, &linkStatus);
    linkInfo = "";

    if (linkStatus == GL_TRUE)
    {
        int infoLength = 0;
        int maxLength = 0;
        glGetProgramiv(programId, GL_INFO_LOG_LENGTH, &maxLength);
        char* infoLog = (char*)SDL_malloc(sizeof(char) * maxLength);
        glGetProgramInfoLog(programId, maxLength, &infoLength, infoLog);

        if (infoLength > 0)
        {
            SDL_Log("Program: %s", infoLog);
            linkInfo = infoLog;
        }
        else
        {
            linkInfo = "Success.";
        }
        SDL_free(infoLog);
    }

    glDeleteShader(vertShaderId);
    glDeleteShader(fragShaderId);

    SDL_free(vertSource);
    SDL_free(fragSource);
}

void Shader::Unload()
{
    if (programId > 0)
    {
        glDeleteProgram(programId);
        programId = 0;
    }

    vertStatus = GL_FALSE;
    vertShaderInfo = "";
    fragStatus = GL_FALSE;
    fragShaderInfo = "";
    linkStatus = GL_FALSE;
    linkInfo = "";
}