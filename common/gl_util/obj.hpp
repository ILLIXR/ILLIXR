#pragma once

#include "../error_util.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include "lib/tiny_obj_loader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "lib/stb_image.h"

namespace ILLIXR {

// Struct which is used for vertex attributes.
// Interleaves position/uv data inside one VBO.
struct vertex_t {
    GLfloat position[3];
    GLfloat uv[2];
};

// Struct for drawable debug objects (scenery, headset visualization, etc)
// Performs its own GL calls. Not indexed.
struct object_t {
    GLuint vbo_handle;
    GLuint num_triangles;
    GLuint texture;
    bool   has_texture;

    void Draw() {
        RAC_ERRNO_MSG("gl_util/obj at start of Draw");

        glBindBuffer(GL_ARRAY_BUFFER, vbo_handle);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void*) offsetof(vertex_t, position));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void*) offsetof(vertex_t, uv));

        if (has_texture) {
            glBindTexture(GL_TEXTURE_2D, texture);
        }

        glDrawArrays(GL_TRIANGLES, 0, num_triangles * 3);

        if (has_texture) {
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        RAC_ERRNO_MSG("gl_util/obj at end of Draw");
    }
};

// Represents a scene obtained from a single OBJ file.
// Multiple objects can reside within the OBJ file; each
// object can have a single diffuse texture. Multi-material
// objects not supported.
class ObjScene {
public:
    // Default constructor for intialization.
    // If the full constructor is not called,
    // Draw() will never do anything.
    ObjScene() {
        successfully_loaded_model = false;
    }

    // obj_dir is the containing directory that should contain
    // the OBJ file, along with any material files and textures.
    //
    // obj_filename is the actual .obj file to be loaded.
    ObjScene(const std::string& obj_dir, const std::string& obj_filename) {
        RAC_ERRNO_MSG("gl_util/obj at start of ObjScene");

        // If any of the following procedures fail to correctly load,
        // we'll set this flag false (for the relevant operation)
        successfully_loaded_model   = true;
        successfully_loaded_texture = true;

        std::string warn, err;

        const std::string obj_dir_term = (obj_dir.back() == '/') ? obj_dir : obj_dir + "/";
        const std::string obj_file     = obj_dir_term + obj_filename;

        // We pass obj_dir as the last argument to LoadObj to let us load
        // any material (.mtl) files associated with the .obj in the same directory.
        bool success = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, obj_file.c_str(), obj_dir_term.c_str());
        if (!warn.empty()) {
#ifndef NDEBUG
            std::cout << "[OBJ WARN] " << warn << std::endl;
#endif
        }
        if (!err.empty()) {
            std::cerr << "[OBJ ERROR] " << err << std::endl;
            successfully_loaded_model = false;
            ILLIXR::abort();
        }
        if (!success) {
            std::cerr << "[OBJ FATAL] Loading of " << obj_filename << " failed." << std::endl;
            successfully_loaded_model = false;
            ILLIXR::abort();
        } else {
            // OBJ file successfully loaded.

            for (size_t mat_idx = 0; mat_idx < materials.size(); mat_idx++) {
                tinyobj::material_t* mp = &materials[mat_idx];
#ifndef NDEBUG
                std::cout << "[OBJ INFO] Loading material named: " << materials[mat_idx].name << std::endl;
                std::cout << "[OBJ INFO] Material texture name: " << materials[mat_idx].diffuse_texname << std::endl;
#endif
                if (mp->diffuse_texname.length() > 0) {
                    // If we haven't loaded the texture yet...
                    if (textures.find(mp->diffuse_texname) == textures.end()) {
                        const std::string filename = obj_dir_term + mp->diffuse_texname;

                        int            x, y, n;
                        unsigned char* texture_data = stbi_load(filename.c_str(), &x, &y, &n, 0);

                        if (texture_data == nullptr) {
#ifndef NDEBUG
                            std::cout << "[OBJ TEXTURE ERROR] Loading of " << filename << "failed." << std::endl;
#endif
                            successfully_loaded_texture = false;
                        } else {
#ifndef NDEBUG
                            std::cout << "[OBJ TEXTURE INFO] Loaded " << filename << ": Resolution (" << x << ", " << y << ")"
                                      << std::endl;
#endif
                            GLuint texture_handle;

                            // Create and bind OpenGL resource.
                            glGenTextures(1, &texture_handle);
                            glBindTexture(GL_TEXTURE_2D, texture_handle);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                            // Configure number of color channels in texture.
                            if (n == 3) {
                                // 3-channel -> RGB
                                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, x, y, 0, GL_RGB, GL_UNSIGNED_BYTE, texture_data);
                            } else if (n == 4) {
                                // 4-channel -> RGBA
                                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, x, y, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture_data);
                            }

                            // Unbind.
                            glBindTexture(GL_TEXTURE_2D, 0);

                            // Insert the texture into our collection of loaded textures.
                            textures.insert(std::make_pair(mp->diffuse_texname, texture_handle));
                        }

                        // Free stbi image regardless of load success.
                        // Once image is uploaded to GPU, we don't
                        // need to keep the CPU-side around anymore.
                        stbi_image_free(texture_data);
                    }
                }
            }

            // Process mesh data.
            // Iterate over "shapes" (objects in .obj file)
            for (size_t shape_idx = 0; shape_idx < shapes.size(); shape_idx++) {
#ifndef NDEBUG
                std::cout << "[OBJ INFO] Num verts in shape: " << shapes[shape_idx].mesh.indices.size() << std::endl;
                std::cout << "[OBJ INFO] Num tris in shape: " << shapes[shape_idx].mesh.indices.size() / 3 << std::endl;
#endif
                // Unified buffer for pos + uv. Interleaving vertex data (good practice!)
                std::vector<vertex_t> buffer;
                // Iterate over triangles
                for (size_t tri_idx = 0; tri_idx < shapes[shape_idx].mesh.indices.size() / 3; tri_idx++) {
                    tinyobj::index_t idx0 = shapes[shape_idx].mesh.indices[3 * tri_idx + 0];
                    tinyobj::index_t idx1 = shapes[shape_idx].mesh.indices[3 * tri_idx + 1];
                    tinyobj::index_t idx2 = shapes[shape_idx].mesh.indices[3 * tri_idx + 2];

                    // Unfortunately we have to unpack/linearize the polygons
                    // because OpenGL can't use OBJ-style indices :/

                    float verts[3][3]; // [vert][xyz]
                    int   f0 = idx0.vertex_index;
                    int   f1 = idx1.vertex_index;
                    int   f2 = idx2.vertex_index;
                    for (int axis = 0; axis < 3; axis++) {
                        verts[0][axis] = attrib.vertices[3 * f0 + axis];
                        verts[1][axis] = attrib.vertices[3 * f1 + axis];
                        verts[2][axis] = attrib.vertices[3 * f2 + axis];
                    }

                    float tex_coords[3][2] = {{0, 0}, {0, 0}, {0, 0}}; // [vert][uv] for each vertex.
                    if (attrib.texcoords.size() > 0) {
                        if ((idx0.texcoord_index >= 0) || (idx1.texcoord_index >= 0) || (idx2.texcoord_index >= 0)) {
                            // Flip Y coord.
                            tex_coords[0][0] = attrib.texcoords[2 * idx0.texcoord_index];
                            tex_coords[0][1] = 1.0f - attrib.texcoords[2 * idx0.texcoord_index + 1];
                            tex_coords[1][0] = attrib.texcoords[2 * idx1.texcoord_index];
                            tex_coords[1][1] = 1.0f - attrib.texcoords[2 * idx1.texcoord_index + 1];
                            tex_coords[2][0] = attrib.texcoords[2 * idx2.texcoord_index];
                            tex_coords[2][1] = 1.0f - attrib.texcoords[2 * idx2.texcoord_index + 1];
                        }
                    }

                    for (int vert = 0; vert < 3; vert++) {
                        buffer.push_back(vertex_t{.position = {verts[vert][0], verts[vert][1], verts[vert][2]},
                                                  .uv       = {tex_coords[vert][0], tex_coords[vert][1]}});
                    }
                }

                object_t newObject;
                newObject.vbo_handle    = 0;
                newObject.num_triangles = 0;
                newObject.has_texture   = false;

                if (shapes[shape_idx].mesh.material_ids.size() >= 0) {
                    std::string texname = materials[shapes[shape_idx].mesh.material_ids[0]].diffuse_texname;
                    if (textures.find(texname) != textures.end()) {
                        // Object has a texture. Tell it what the GL handle is!
                        newObject.has_texture = true;
                        newObject.texture     = textures[texname];
                    }
                }

                if (buffer.size() > 0) {
                    // Create/bind/fill vbo.
                    glGenBuffers(1, &newObject.vbo_handle);
                    glBindBuffer(GL_ARRAY_BUFFER, newObject.vbo_handle);
                    glBufferData(GL_ARRAY_BUFFER, buffer.size() * sizeof(vertex_t), &buffer.at(0), GL_STATIC_DRAW);
                    glBindBuffer(GL_ARRAY_BUFFER, 0);

                    // Compute the number of triangles for this object.
                    newObject.num_triangles = buffer.size() / 3;
                }

                objects.push_back(newObject);
            }
        }

        RAC_ERRNO_MSG("gl_util/obj at bottom of ObjScene constructor");
    }

    void Draw() {
        if (successfully_loaded_model) {
            for (auto obj : objects) {
                obj.Draw();
            }
        }
    }

    bool successfully_loaded_model   = false;
    bool successfully_loaded_texture = false;

    tinyobj::attrib_t                attrib;
    std::vector<tinyobj::shape_t>    shapes;
    std::vector<tinyobj::material_t> materials;

    std::map<std::string, GLuint> textures;

    std::vector<object_t> objects;
};
} // namespace ILLIXR
