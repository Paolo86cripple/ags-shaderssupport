#pragma once

#include <GL/gl.h>
#include <GL/glext.h>

namespace ags_shader_gl_compat {

inline void get_vertex_attrib_fv(GLuint index, GLenum pname, GLfloat *params) {
    if (index == 0 && pname == GL_CURRENT_VERTEX_ATTRIB) {
        if (params) {
            params[0] = 0.f;
            params[1] = 0.f;
            params[2] = 0.f;
            params[3] = 1.f;
        }
        return;
    }
    ::glGetVertexAttribfv(index, pname, params);
}

inline void vertex_attrib4fv(GLuint index, const GLfloat *values) {
    // In the legacy compatibility profile, generic attribute 0 has no
    // meaningful current value to preserve. Array state is restored separately.
    if (index == 0) return;
    ::glVertexAttrib4fv(index, values);
}

} // namespace ags_shader_gl_compat

#define glGetVertexAttribfv ags_shader_gl_compat::get_vertex_attrib_fv
#define glVertexAttrib4fv ags_shader_gl_compat::vertex_attrib4fv
