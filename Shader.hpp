// made by   m a t i T e c h n o
// todo: inotify
// in implementation file:
//                        #define SHADER_IMPLEMENTATION
//                        #include "glad.h"
//                        #include "Shader.h"

// shader source format (order does not matter):
//                                              VERTEX
//                                              ...
//                                              GEOMETRY (optional)
//                                              ...
//                                              FRAGMENT
//                                              ...
//                                              or
//                                              COMPUTE
//                                              ...

#pragma once

#include <map>
#include <string>

namespace sh
{

class Shader
{
public:
    using GLint = int;
    using GLuint = unsigned int;

    Shader(std::string filename);
    Shader(const std::string& source, std::string id);
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& rhs);
    Shader& operator=(Shader&& rhs);

    bool isValid() const {return programId_;}
    void bind();
    GLint getUniformLocation(const std::string& uniformName);

private:
    std::string id_;
    GLuint programId_ = 0;
    std::map<std::string, GLint> uniformLocations_;

    void initialize(const std::string& source);
};

} // namespace sh

#ifdef SHADER_IMPLEMENTATION

#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <algorithm>

namespace sh
{

struct Error
{
    bool isError_;
    std::string errorMessage_;
};

template<bool isProgram>
Error getError(GLuint id, GLenum flag)
{
    GLint success;

    if constexpr (isProgram)
        glGetProgramiv(id, flag, &success);
    else
        glGetShaderiv(id, flag, &success);

    if(success == GL_TRUE)
        return {false, {}};

    GLint length;
    if constexpr (isProgram)
        glGetProgramiv(id, GL_INFO_LOG_LENGTH, &length);
    else
        glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length);

    if(!length)
        return {false, {}};

    --length;
    std::string log(length, '\0');

    if constexpr (isProgram)
        glGetProgramInfoLog(id, length, nullptr, log.data());
    else
        glGetShaderInfoLog(id, length, nullptr, log.data());

    return {true, log};
}

GLuint createAndCompileShader(GLenum type, const std::string& source)
{
    auto id = glCreateShader(type);
    auto* str = source.c_str();
    glShaderSource(id, 1, &str, nullptr);
    glCompileShader(id);
    return id;
}

Shader::Shader(std::string filename)
{
    std::ifstream file(filename);
    if(!file.is_open())
    {
        std::cout << "sh::Shader, " << id_ << ": could not open file [" << filename
                                    << ']' << std::endl;
        return;
    }

    std::stringstream stringstream;
    stringstream << file.rdbuf();
    id_ = std::move(filename);
    initialize(stringstream.str());
}

Shader::Shader(const std::string& source, std::string id):
    id_(std::move(id))
{
    initialize(source);
}

Shader::~Shader()
{
    if(programId_)
        glDeleteProgram(programId_);
}

Shader::Shader(Shader&& rhs):
    id_(std::move(rhs.id_)),
    programId_(rhs.programId_),
    uniformLocations_(std::move(rhs.uniformLocations_))
{
    rhs.programId_ = 0;
}

Shader& Shader::operator=(Shader&& rhs)
{
    if(this == &rhs)
        return *this;

    this->~Shader();

    id_ = std::move(rhs.id_);
    programId_ = rhs.programId_;
    uniformLocations_ = std::move(rhs.uniformLocations_);

    rhs.programId_ = 0;

    return *this;
}

void Shader::bind()
{
    glUseProgram(programId_);
}

GLint Shader::getUniformLocation(const std::string& uniformName)
{
    auto it = uniformLocations_.find(uniformName);
    if(it == uniformLocations_.end())
    {
        std::cout << "sh::Shader, " << id_ << ": not active uniform ["
                  << uniformName << ']' << std::endl;
        return {};
    }

    return it->second;
}

void Shader::initialize(const std::string& source)
{
    struct ShaderType
    {
        GLenum value_;
        std::string name_;
    };

    struct ShaderData
    {
        std::size_t sourceStart_;
        const ShaderType* type_;
    };

    static const ShaderType shaderTypes[] = {{GL_VERTEX_SHADER,   "VERTEX"},
                                            {GL_GEOMETRY_SHADER, "GEOMETRY"},
                                            {GL_FRAGMENT_SHADER, "FRAGMENT"},
                                            {GL_COMPUTE_SHADER,  "COMPUTE"}};

    std::vector<ShaderData> shaderData;

    for(auto& shaderType: shaderTypes)
    {
        if(auto pos = source.find(shaderType.name_); pos != std::string::npos)
            shaderData.push_back({pos + shaderType.name_.size(), &shaderType});
    }

    std::sort(shaderData.begin(), shaderData.end(),
              [](ShaderData& l, ShaderData& r)
              {return l.sourceStart_ < r.sourceStart_;});

    std::vector<GLuint> shaders;

    auto compilationError = false;

    for(auto it = shaderData.begin(); it != shaderData.end(); ++it)
    {
        std::size_t count;
        if(it == shaderData.end() - 1)
            count = std::string::npos;
        else
        {
            auto nextIt = it + 1;
            count = nextIt->sourceStart_ - nextIt->type_->name_.size()
                    - it->sourceStart_;
        }

        shaders.push_back(createAndCompileShader(it->type_->value_,
                                                 source.substr(it->sourceStart_,
                                                               count)));

        auto error = getError<false>(shaders.back(), GL_COMPILE_STATUS);
        if(error.isError_)
        {
            std::cout << "sh::Shader, " << id_ << ": " << it->type_->name_.c_str()
                      << " shader compilation failed, error log:\n"
                      << error.errorMessage_ << std::endl;

            compilationError = true;
        }
    }

    if(compilationError)
    {
        for(auto shader: shaders)
            glDeleteShader(shader);

        return;
    }

    programId_ = glCreateProgram();

    for(auto shader: shaders)
        glAttachShader(programId_, shader);

    glLinkProgram(programId_);

    for(auto shader: shaders)
    {
        glDetachShader(programId_, shader);
        glDeleteShader(shader);
    }

    auto error = getError<true>(programId_, GL_LINK_STATUS);
    if(error.isError_)
    {
        std::cout << "sh::Shader, " << id_ << ": program linking failed, error log:\n"
                  << error.errorMessage_ << std::endl;

        glDeleteProgram(programId_);
        programId_ = 0;
        return;
    }

    GLint numUniforms;
    glGetProgramiv(programId_, GL_ACTIVE_UNIFORMS, &numUniforms);

    std::vector<char> uniformName(256);

    for(int i = 0; i < numUniforms; ++i)
    {
        GLint dum1;
        GLenum dum2;

        glGetActiveUniform(programId_, i, uniformName.size(), nullptr, &dum1, &dum2,
                           uniformName.data());

        auto uniformLocation = glGetUniformLocation(programId_, uniformName.data());
        uniformLocations_[uniformName.data()] = uniformLocation;
    }
}

} // namespace sh

#endif // SHADER_IMPLEMENTATION
