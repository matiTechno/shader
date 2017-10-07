// made by   m a t i T e c h n o
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
// code is exception free

#pragma once

#include <map>
#include <string>
#include <experimental/filesystem>

namespace sh
{

namespace fs = std::experimental::filesystem;

// does bind in constructors and reload functions
class Shader
{
public:
    enum {FilePollDelay = 1};
    using GLint = int;
    using GLuint = unsigned int;

    Shader(const std::string& filename);
    Shader(const std::string& source, const std::string& id);
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& rhs);
    Shader& operator=(Shader&& rhs);

    bool isValid() const {return programId_;}
    void bind();
    GLint getUniformLocation(const std::string& uniformName) const;

    // return true if changes in file were detected
    // after successful reload:
    //                         * shader must be rebound
    //                         * all uniform locations are invalidated
    bool reload();
    bool hotReload(float frameTimeS);

private:
    std::string id_;
    GLuint programId_ = 0;
    std::map<std::string, GLint> uniformLocations_;
    bool isSourceFromFile_;
    fs::file_time_type fileLastWriteTime_;
    float accumulator_ = 0.f;

    void swapProgram(const std::string& source);
};

} // namespace sh

#ifdef SHADER_IMPLEMENTATION

#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <algorithm>
#include <optional>

namespace sh
{

std::optional<std::string> loadSourceFromFile(const std::string& filename)
{
    std::ifstream file(filename);

    if(!file.is_open())
    {
        std::cout << "sh::Shader, could not open file = " << filename << std::endl;
        return {};
    }

    std::stringstream stringstream;
    stringstream << file.rdbuf();
    return stringstream.str();
}

std::optional<fs::file_time_type> getFileLastWriteTime(const std::string& filename)
{
    std::error_code ec;
    auto time = fs::last_write_time(filename, ec);

    if(ec)
    {
        std::cout << "sh::Shader, last_write_time() failed on file = "
                  << filename << std::endl;

        return {};
    }

    return time;
}

Shader::Shader(const std::string& filename):
    id_(filename),
    isSourceFromFile_(true)
{
    if(auto source = loadSourceFromFile(filename))
    {
        if(auto time = getFileLastWriteTime(filename))
            fileLastWriteTime_ = *time;

        swapProgram(*source);
    }
}

Shader::Shader(const std::string& source, const std::string& id):
    id_(id),
    isSourceFromFile_(false)
{
    swapProgram(source);
}

Shader::~Shader()
{
    if(programId_)
        glDeleteProgram(programId_);
}

Shader::Shader(Shader&& rhs):
    id_(std::move(rhs.id_)),
    programId_(rhs.programId_),
    uniformLocations_(std::move(rhs.uniformLocations_)),
    isSourceFromFile_(rhs.isSourceFromFile_),
    fileLastWriteTime_(rhs.fileLastWriteTime_),
    accumulator_(rhs.accumulator_)
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
    isSourceFromFile_ = rhs.isSourceFromFile_;
    fileLastWriteTime_ = std::move(rhs.fileLastWriteTime_);
    accumulator_ = rhs.accumulator_;

    rhs.programId_ = 0;

    return *this;
}

void Shader::bind()
{
    glUseProgram(programId_);
}

GLint Shader::getUniformLocation(const std::string& uniformName) const
{
    auto it = uniformLocations_.find(uniformName);
    if(it == uniformLocations_.end())
    {
        std::cout << "sh::Shader, " << id_ << ": not active uniform = "
                  << uniformName << std::endl;
        return {};
    }

    return it->second;
}

bool Shader::reload()
{
    if(!isSourceFromFile_)
    {
        std::cout << "sh::Shader, " << id_ << ": reload() failed, shader was not "
                     "constructed from file" << std::endl;

        return false;
    }
    
    auto time = getFileLastWriteTime(id_);
    
    if(!time || *time == fileLastWriteTime_)
        return false;
    
    fileLastWriteTime_ = *time;

    if(auto source = loadSourceFromFile(id_))
        swapProgram(*source);
    
    return true;
}

bool Shader::hotReload(float frameTimeS)
{
    accumulator_ += frameTimeS;
    if(accumulator_ < FilePollDelay)
        return false;
    
    accumulator_ = 0.f;
    return reload();
}

template<bool isProgram>
std::optional<std::string> getError(GLuint id, GLenum flag)
{
    GLint success;

    if constexpr (isProgram)
        glGetProgramiv(id, flag, &success);
    else
        glGetShaderiv(id, flag, &success);

    if(success == GL_TRUE)
        return {};

    GLint length;
    if constexpr (isProgram)
        glGetProgramiv(id, GL_INFO_LOG_LENGTH, &length);
    else
        glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length);

    if(!length)
        return "";

    --length;
    std::string log(length, '\0');

    if constexpr (isProgram)
        glGetProgramInfoLog(id, length, nullptr, log.data());
    else
        glGetShaderInfoLog(id, length, nullptr, log.data());

    return log;
}

// shader must be cleaned by caller with glDeleteShader()
GLuint createAndCompileShader(GLenum type, const std::string& source)
{
    auto id = glCreateShader(type);
    auto* str = source.c_str();
    glShaderSource(id, 1, &str, nullptr);
    glCompileShader(id);
    return id;
}

// returns 0 on error
// when return value != 0 program must be cleaned by caller with glDeleteProgram()
GLuint createProgram(const std::string& source, const std::string& id)
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

        if(auto error = getError<false>(shaders.back(), GL_COMPILE_STATUS))
        {
            std::cout << "sh::Shader, " << id << ": " << it->type_->name_.c_str()
                      << " shader compilation failed\n"
                      << *error << std::endl;

            compilationError = true;
        }
    }

    if(compilationError)
    {
        for(auto shader: shaders)
            glDeleteShader(shader);

        return 0;
    }

    auto program = glCreateProgram();

    for(auto shader: shaders)
        glAttachShader(program, shader);

    glLinkProgram(program);

    for(auto shader: shaders)
    {
        glDetachShader(program, shader);
        glDeleteShader(shader);
    }

    if(auto error = getError<true>(program, GL_LINK_STATUS))
    {
        std::cout << "sh::Shader, " << id << ": program linking failed\n"
                  << *error << std::endl;

        glDeleteProgram(program);
        return 0;
    }

    return program;
}

void Shader::swapProgram(const std::string& source)
{
    auto newProgramId  = createProgram(source, id_);
    
    if(!newProgramId)
        return;

    this->~Shader();
    uniformLocations_.clear();

    programId_ = newProgramId;

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

    bind();
}

} // namespace sh

#endif // SHADER_IMPLEMENTATION
