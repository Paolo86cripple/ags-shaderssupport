#include "shader_pipeline_v3.h"
#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char **argv)
{
    const char *shader = argc > 1 ? argv[1] : "shaders/identity.glsl";
    const bool expect_invert = argc > 2 && std::string(argv[2]) == "invert";
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 2;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window *window = SDL_CreateWindow("ags-shader-test",0,0,64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    if(!window){std::fprintf(stderr,"shader test: %s\n",SDL_GetError());SDL_Quit();return 3;}
    SDL_GLContext context=SDL_GL_CreateContext(window);
    if(!context){std::fprintf(stderr,"shader test: %s\n",SDL_GetError());SDL_DestroyWindow(window);SDL_Quit();return 4;}
    const char *version=(const char*)glGetString(GL_VERSION); const char *renderer=(const char*)glGetString(GL_RENDERER);
    std::fprintf(stderr,"shader test: OpenGL %s / %s\n",version?version:"unknown",renderer?renderer:"unknown");
    GLuint source_texture=0; glGenTextures(1,&source_texture); glBindTexture(GL_TEXTURE_2D,source_texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    const unsigned char pixel[4]={32,64,128,255}; glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    GLenum err=glGetError(); if(err!=GL_NO_ERROR){std::fprintf(stderr,"shader test: texture setup GL error 0x%x\n",err);return 5;}
    ShaderPipelineV3 pipeline; std::string error; if(!pipeline.load(shader,error)){std::fprintf(stderr,"shader test: load failed: %s\n",error.c_str());return 6;}
    std::fprintf(stderr,"shader test: pipeline loaded\n"); pipeline.apply(source_texture,1,1,64,64); glFinish();
    err=glGetError(); if(err!=GL_NO_ERROR){std::fprintf(stderr,"shader test: pipeline GL error 0x%x\n",err);return 7;}
    unsigned char result[4]={0,0,0,0}; glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,result); err=glGetError();
    if(err!=GL_NO_ERROR){std::fprintf(stderr,"shader test: readback GL error 0x%x\n",err);return 8;}
    const unsigned char expected[4]={expect_invert?223:32,expect_invert?191:64,expect_invert?127:128,255};
    const bool ok=result[0]==expected[0]&&result[1]==expected[1]&&result[2]==expected[2]&&result[3]==expected[3];
    std::fprintf(stderr,ok?"shader test: PASS (%u,%u,%u,%u)\n":"shader test: FAIL got (%u,%u,%u,%u), expected (%u,%u,%u,%u)\n",result[0],result[1],result[2],result[3],expected[0],expected[1],expected[2],expected[3]);
    glDeleteTextures(1,&source_texture); SDL_GL_DeleteContext(context); SDL_DestroyWindow(window); SDL_Quit(); return ok?0:9;
}
