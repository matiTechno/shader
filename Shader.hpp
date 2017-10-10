// made by   m a t i T e c h n o
// compiles with gcc 7.2.0
// in the implementation file:

// #define SHADER_IMPLEMENTATION
// #include "glad.h" or "glew.h" or ...
// #include "Shader.h"

// shader source format (order does not matter):

// VERTEX
// ...
// GEOMETRY (optional)
// ...
// FRAGMENT
// ...
// or
// COMPUTE
// ...
// or
// INCLUDE "vertex.glsl"
// FRAGMENT
// ...

// code is exception free
// warning: preprocessor parsing is naive

#pragma once

#include <string>
#include <set>
#include <map>
#include <experimental/filesystem>

namespace sh
{

namespace fs = std::experimental::filesystem;

class Shader
{
public:
    using GLint = int;
    using GLuint = unsigned int;

    Shader(const std::string& filename, bool hotReload = false);
    Shader(const std::string& source, const char* id);

    bool isValid() const {return program_.getId();}

    GLint getUniformLocation(const std::string& uniformName) const;

    // after successful reload:
    //                         * shader must be rebound
    //                         * all uniform locations are invalidated
    // on failure:
    //                         * previous state remains
    //
    void reload(); // does not check if file was modified

    void bind(); // if hotReload is on and file was modified does reload

private:
    class Program
    {
    public:
        Program(): id_(0) {}
        Program(GLuint id): id_(id) {}
        ~Program();
        Program(const Program&) = delete;
        Program& operator=(const Program&) = delete;
        Program(Program&& rhs): id_(rhs.id_) {rhs.id_ = 0;}

        Program& operator=(Program&& rhs)
        {
            if(this == &rhs)
                return *this;
            this->~Program();
            id_ = rhs.id_;
            rhs.id_ = 0;
            return *this;
        }

        GLuint getId() const {return id_;}

    private:
        GLuint id_;
    };

    std::string id_;
    bool hotReload_;
    Program program_;
    fs::file_time_type fileLastWriteTime_;
    std::map<std::string, GLint> uniformLocations_;
    mutable std::set<std::string> inactiveUniforms_;

    // returns true on success
    bool swapProgram(const std::string& source);
};

} // namespace sh

#ifdef SHADER_IMPLEMENTATION

#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace sh
{
    
Shader::Program::~Program() {if(id_) glDeleteProgram(id_);}

std::string loadSourceFromFile(const std::string& filename)
{
    std::ifstream file(filename);

    if(!file.is_open())
    {
        std::cout << "sh::Shader: could not open file = " << filename << std::endl;
        return {};
    }

    std::stringstream stringstream;
    stringstream << file.rdbuf();
    auto source = stringstream.str();
    
    static const std::string includeDirective = "INCLUDE";

    auto lineFirst = source.find(includeDirective);

    if(lineFirst != std::string::npos)
    {
        auto lineLast = source.find('\n', lineFirst) + 1;

        auto filenameFirst = source.find('"', lineFirst + includeDirective.size()) + 1;

        auto filenameCount = source.find('"', filenameFirst) - filenameFirst;
        
        source.insert(lineLast,
                      loadSourceFromFile(source.substr(filenameFirst, filenameCount)));

        source.erase(lineFirst, lineLast - lineFirst);
    }

    return source;
}

fs::file_time_type getFileLastWriteTime(const std::string& filename)
{
    std::error_code ec;
    auto time = fs::last_write_time(filename, ec);

    if(ec)
    {
        std::cout << "sh::Shader: last_write_time() failed, file = "
                  << filename << std::endl;
    }

    return time;
}

Shader::Shader(const std::string& filename, bool hotReload):
    id_(filename),
    hotReload_(hotReload)
{
    fileLastWriteTime_ = getFileLastWriteTime(filename);

    if(auto source = loadSourceFromFile(filename); source.size())
        swapProgram(source);
}

Shader::Shader(const std::string& source, const char* id):
    id_(id),
    hotReload_(false)
{
    swapProgram(source);
}

void Shader::bind()
{
    if(hotReload_)
    {
        if(auto time = getFileLastWriteTime(id_); time > fileLastWriteTime_)
        {
            fileLastWriteTime_ = time;
            
            if(auto source = loadSourceFromFile(id_); source.size())
                if(swapProgram(source))
                    std::cout << "sh::Shader, " << id_
                              << ": hot reload succeeded" << std::endl;
        }
    }

    glUseProgram(program_.getId());
}

GLint Shader::getUniformLocation(const std::string& uniformName) const
{
    auto it = uniformLocations_.find(uniformName);

    if(it == uniformLocations_.end() &&
        inactiveUniforms_.find(uniformName) == inactiveUniforms_.end())
    {
        std::cout << "sh::Shader, " << id_ << ": inactive uniform = "
                  << uniformName << std::endl;

        inactiveUniforms_.insert(uniformName);

        return 666;
    }

    return it->second;
}

void Shader::reload()
{
    if(auto time = getFileLastWriteTime(id_); time > fileLastWriteTime_)
        fileLastWriteTime_ = time;

    if(auto source = loadSourceFromFile(id_); source.size())
        if(swapProgram(source))
            std::cout << "sh::Shader, " << id_ << ": reload succeeded" << std::endl;
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
        GLenum value;
        std::string name;
    };

    static const ShaderType shaderTypes[] = {{GL_VERTEX_SHADER,   "VERTEX"},
                                             {GL_GEOMETRY_SHADER, "GEOMETRY"},
                                             {GL_FRAGMENT_SHADER, "FRAGMENT"},
                                             {GL_COMPUTE_SHADER,  "COMPUTE"}};
    
    struct ShaderData
    {
        std::size_t sourceStart;
        const ShaderType* type;
    };

    std::vector<ShaderData> shaderData;

    for(auto& shaderType: shaderTypes)
    {
        if(auto pos = source.find(shaderType.name); pos != std::string::npos)
            shaderData.push_back({pos + shaderType.name.size(), &shaderType});
    }

    std::sort(shaderData.begin(), shaderData.end(),
              [](ShaderData& l, ShaderData& r)
              {return l.sourceStart < r.sourceStart;});

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
            count = nextIt->sourceStart - nextIt->type->name.size()
                    - it->sourceStart;
        }

        shaders.push_back(createAndCompileShader(it->type->value,
                                                 source.substr(it->sourceStart,
                                                               count)));

        if(auto error = getError<false>(shaders.back(), GL_COMPILE_STATUS))
        {
            std::cout << "sh::Shader, " << id << ": " << it->type->name.c_str()
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

bool Shader::swapProgram(const std::string& source)
{
    auto newProgram = createProgram(source, id_);
    if(!newProgram)
        return false;
    
    program_ = Program(newProgram);
    
    uniformLocations_.clear();
    inactiveUniforms_.clear();

    GLint numUniforms;
    glGetProgramiv(program_.getId(), GL_ACTIVE_UNIFORMS, &numUniforms);

    std::vector<char> uniformName(256);

    for(int i = 0; i < numUniforms; ++i)
    {
        GLint dum1;
        GLenum dum2;

        glGetActiveUniform(program_.getId(), i, uniformName.size(), nullptr,
                           &dum1, &dum2, uniformName.data());

        auto uniformLocation = glGetUniformLocation(program_.getId(),
                                                    uniformName.data());

        uniformLocations_[uniformName.data()] = uniformLocation;
    }

    return true;
}

} // namespace sh

#endif // SHADER_IMPLEMENTATION
