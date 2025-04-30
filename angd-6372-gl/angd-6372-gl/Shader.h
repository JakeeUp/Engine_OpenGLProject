#pragma once

#include "GL/glew.h"
#include <SDL3/SDL.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>




#include <string>
struct Shader
{
    GLint vertStatus = GL_TRUE;
    std::string vertShaderInfo = "";
    GLint fragStatus = GL_TRUE;
    std::string fragShaderInfo = "";
    std::string linkInfo = "";
    GLint linkStatus = GL_TRUE;

    GLuint programId = 0;


    GLint GetUniformLocation(const char* name);


    void SetUnifromFLoat(const GLint id, const float value);
    void SetUniformInt(const GLint id, const int value);

    void SetUniformVec4(const GLint id, const glm::vec4& value);
    void SetUniformVec3(const GLint id, const glm::vec3& value);
    void SetUniformVec2(const GLint id, const glm::vec2& value);
    void SetUniformMat4(const GLint id, const glm::mat4& value);


	void LoadShaderProgram(const char* vertPath, const char* fragPath);
    void Unload();
};