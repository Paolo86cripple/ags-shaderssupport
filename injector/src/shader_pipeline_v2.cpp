#include "shader_pipeline_v2.h"
#include <GL/gl.h>
#include <GL/glext.h>
#include <SDL2/SDL.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
const char *kVertex = "#version 120\nattribute vec2 aPosition; attribute vec2 aTexCoord; varying vec2 vTexCoord; void main(){vTexCoord=aTexCoord;gl_Position=vec4(aPosition,0.0,1.0);}";
struct V { float x,y,u,v; };
const V quad[]={{-1,-1,0,0},{1,-1,1,0},{-1,1,0,1},{1,1,1,1}};
std::string trim(std::string s){auto a=s.find_first_not_of(" \t\r\n");if(a==std::string::npos)return{};auto b=s.find_last_not_of(" \t\r\n");return s.substr(a,b-a+1);}
std::string lower(std::string s){for(char &c:s)c=(char)std::tolower((unsigned char)c);return s;}
bool suffix(const std::string&s,const char*x){if(s.size()<std::strlen(x))return false;return lower(s.substr(s.size()-std::strlen(x)))==lower(x);}
std::string dir(const std::string&p){auto n=p.find_last_of("/\\");return n==std::string::npos?".":p.substr(0,n);}
std::string join(const std::string&d,std::string p){if(!p.empty()&&p[0]=='/')return p; if(p.size()>1&&p[1]==':')return p; return d+"/"+p;}
std::string unquote(std::string s){s=trim(s);if(s.size()>1&&s.front()=='"'&&s.back()=='"')s=s.substr(1,s.size()-2);return s;}
float num(const std::string&s,float d){char*e=nullptr;auto v=std::strtof(unquote(s).c_str(),&e);return e&&*e=='\0'?v:d;}
bool boolean(const std::string&s,bool d){auto v=lower(unquote(s));return v=="true"||v=="1"?true:v=="false"||v=="0"?false:d;}
std::string stage(const std::string&s,const char*n){std::string d=std::string("#define ")+n+"\n";auto p=s.find("#version");if(p!=std::string::npos){auto e=s.find('\n',p);if(e!=std::string::npos)return s.substr(0,e+1)+d+s.substr(e+1);}return d+s;}
bool combined(const std::string&s){return s.find("defined(VERTEX)")!=std::string::npos&&s.find("defined(FRAGMENT)")!=std::string::npos;}

typedef void (*GenFramebuffersProc)(GLsizei, GLuint*);
typedef void (*DeleteFramebuffersProc)(GLsizei, const GLuint*);
typedef void (*BindFramebufferProc)(GLenum, GLuint);
typedef void (*FramebufferTexture2DProc)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (*CheckFramebufferStatusProc)(GLenum);

GenFramebuffersProc pGenFramebuffers=nullptr;
DeleteFramebuffersProc pDeleteFramebuffers=nullptr;
BindFramebufferProc pBindFramebuffer=nullptr;
FramebufferTexture2DProc pFramebufferTexture2D=nullptr;
CheckFramebufferStatusProc pCheckFramebufferStatus=nullptr;

bool load_fbo_functions(std::string& error)
{
    if (pGenFramebuffers && pDeleteFramebuffers && pBindFramebuffer &&
        pFramebufferTexture2D && pCheckFramebufferStatus)
        return true;

    pGenFramebuffers = reinterpret_cast<GenFramebuffersProc>(SDL_GL_GetProcAddress("glGenFramebuffersEXT"));
    pDeleteFramebuffers = reinterpret_cast<DeleteFramebuffersProc>(SDL_GL_GetProcAddress("glDeleteFramebuffersEXT"));
    pBindFramebuffer = reinterpret_cast<BindFramebufferProc>(SDL_GL_GetProcAddress("glBindFramebufferEXT"));
    pFramebufferTexture2D = reinterpret_cast<FramebufferTexture2DProc>(SDL_GL_GetProcAddress("glFramebufferTexture2DEXT"));
    pCheckFramebufferStatus = reinterpret_cast<CheckFramebufferStatusProc>(SDL_GL_GetProcAddress("glCheckFramebufferStatusEXT"));

    if (!pGenFramebuffers || !pDeleteFramebuffers || !pBindFramebuffer ||
        !pFramebufferTexture2D || !pCheckFramebufferStatus)
    {
        error = "required EXT_framebuffer_object functions are unavailable";
        return false;
    }
    return true;
}
}
ShaderPipelineV2::~ShaderPipelineV2(){clear();}
void ShaderPipelineV2::destroy_target(Target&t){if(t.fbo){pDeleteFramebuffers(t.fbo?1:0,&t.fbo);}if(t.texture)glDeleteTextures(1,&t.texture);t=Target();}
void ShaderPipelineV2::clear(){for(auto&p:_passes)if(p.program)glDeleteProgram(p.program);_passes.clear();if(pDeleteFramebuffers){destroy_target(_targets[0]);destroy_target(_targets[1]);}else{_targets[0]=Target();_targets[1]=Target();}_frame_count=0;}
bool ShaderPipelineV2::has_suffix(const std::string&s,const std::string&e){return suffix(s,e.c_str());}
std::string ShaderPipelineV2::parent_dir(const std::string&s){return dir(s);}
std::string ShaderPipelineV2::join_path(const std::string&a,const std::string&b){return join(a,b);}
std::string ShaderPipelineV2::trim(std::string s){return ::trim(s);}
std::string ShaderPipelineV2::lower(std::string s){return ::lower(s);}
bool ShaderPipelineV2::parse_bool(const std::string&s,bool d){return boolean(s,d);}
float ShaderPipelineV2::parse_float(const std::string&s,float d){return num(s,d);}
int ShaderPipelineV2::parse_int(const std::string&s,int d){char*e=nullptr;auto v=std::strtol(unquote(s).c_str(),&e,10);return e&&*e=='\0'?(int)v:d;}
std::string ShaderPipelineV2::make_stage_source(const std::string&s,const char*n){return stage(s,n);}
bool ShaderPipelineV2::is_combined_shader(const std::string&s){return combined(s);}
int ShaderPipelineV2::resolve_dimension(ScaleType t,float scale,int source,int viewport){switch(t){case ScaleType::Viewport:return std::max(1,(int)std::lround(viewport*scale));case ScaleType::Absolute:return std::max(1,(int)std::lround(scale));default:return std::max(1,(int)std::lround(source*scale));}}
bool ShaderPipelineV2::load_text(const std::string&p,std::string&t,std::string&e)const{std::ifstream f(p.c_str(),std::ios::binary);if(!f){e="cannot read shader: "+p;return false;}std::ostringstream s;s<<f.rdbuf();t=s.str();if(t.empty()){e="empty shader: "+p;return false;}return true;}
bool ShaderPipelineV2::compile_shader(unsigned type,const std::string&s,unsigned&sh,std::string&e)const{sh=glCreateShader((GLenum)type);if(!sh){e="glCreateShader failed";return false;}auto p=s.c_str();glShaderSource(sh,1,&p,nullptr);glCompileShader(sh);GLint ok=0;glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);if(ok==GL_TRUE)return true;GLint n=0;glGetShaderiv(sh,GL_INFO_LOG_LENGTH,&n);std::vector<char>log((size_t)std::max(n,1));if(n)glGetShaderInfoLog(sh,n,nullptr,log.data());e=log.data();glDeleteShader(sh);sh=0;return false;}
bool ShaderPipelineV2::create_program(const std::string&v,const std::string&f,unsigned&p,std::string&e)const{unsigned vs=0,fs=0;if(!compile_shader(GL_VERTEX_SHADER,v,vs,e))return false;if(!compile_shader(GL_FRAGMENT_SHADER,f,fs,e)){glDeleteShader(vs);return false;}p=glCreateProgram();if(!p){glDeleteShader(vs);glDeleteShader(fs);e="glCreateProgram failed";return false;}glAttachShader(p,vs);glAttachShader(p,fs);glBindAttribLocation(p,0,"aPosition");glBindAttribLocation(p,1,"aTexCoord");glLinkProgram(p);glDeleteShader(vs);glDeleteShader(fs);GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(ok==GL_TRUE)return true;GLint n=0;glGetProgramiv(p,GL_INFO_LOG_LENGTH,&n);std::vector<char>log((size_t)std::max(n,1));if(n)glGetProgramInfoLog(p,n,nullptr,log.data());e=log.data();glDeleteProgram(p);p=0;return false;}
bool ShaderPipelineV2::add_pass(const std::string&p,const Pass*preset,std::string&e){std::string s;if(!load_text(p,s,e))return false;Pass x;if(preset)x=*preset;x.source_path=p;if(combined(s)){if(!create_program(stage(s,"VERTEX"),stage(s,"FRAGMENT"),x.program,e))return false;}else if(!create_program(kVertex,s,x.program,e))return false;x.texture=glGetUniformLocation(x.program,"uTexture");if(x.texture<0)x.texture=glGetUniformLocation(x.program,"Texture");x.input_size=glGetUniformLocation(x.program,"uInputSize");if(x.input_size<0)x.input_size=glGetUniformLocation(x.program,"InputSize");x.texture_size=glGetUniformLocation(x.program,"TextureSize");x.output_size=glGetUniformLocation(x.program,"uOutputSize");if(x.output_size<0)x.output_size=glGetUniformLocation(x.program,"OutputSize");x.frame_count=glGetUniformLocation(x.program,"uFrameCount");if(x.frame_count<0)x.frame_count=glGetUniformLocation(x.program,"FrameCount");x.frame_direction=glGetUniformLocation(x.program,"FrameDirection");_passes.push_back(x);return true;}
bool ShaderPipelineV2::parse_chain(const std::string&p,std::vector<std::string>&out,std::string&e)const{std::string s;if(!load_text(p,s,e))return false;auto d=dir(p);std::istringstream in(s);std::string l;while(std::getline(in,l)){l=trim(l);if(l.empty()||l[0]=='#')continue;if(l.compare(0,5,"pass=")==0){l=unquote(trim(l.substr(5)));if(!l.empty())out.push_back(join(d,l));}}if(out.empty()){e="shader chain has no passes: "+p;return false;}return true;}
bool ShaderPipelineV2::parse_glslp(const std::string&p,std::vector<Pass>&out,std::string&e)const{std::string s;if(!load_text(p,s,e))return false;auto d=dir(p);std::istringstream in(s);std::string l;int n=-1;struct E{std::string path;Pass pass;};std::vector<E> a;while(std::getline(in,l)){l=trim(l);if(l.empty()||l[0]=='#')continue;auto q=l.find('=');if(q==std::string::npos)continue;auto k=trim(l.substr(0,q));auto v=unquote(l.substr(q+1));if(k=="shaders"){n=parse_int(v,-1);continue;}if(k.rfind("shader",0)==0&&k.size()>6&&std::isdigit((unsigned char)k[6])){int i=parse_int(k.substr(6),-1);if(i>=0){if((size_t)i>=a.size())a.resize((size_t)i+1);a[i].path=join(d,v);}continue;}auto pos=k.find_last_not_of("0123456789");if(pos==std::string::npos||pos+1>=k.size())continue;int i=parse_int(k.substr(pos+1),-1);if(i<0)continue;if((size_t)i>=a.size())a.resize((size_t)i+1);auto base=k.substr(0,pos+1);if(base=="filter_linear")a[i].pass.filter_linear=parse_bool(v,true);else if(base=="scale")a[i].pass.scale_x=a[i].pass.scale_y=parse_float(v,1);else if(base=="scale_x")a[i].pass.scale_x=parse_float(v,1);else if(base=="scale_y")a[i].pass.scale_y=parse_float(v,1);else if(base=="scale_type"){auto t=lower(v);a[i].pass.scale_type_x=a[i].pass.scale_type_y=t=="viewport"?ScaleType::Viewport:t=="absolute"?ScaleType::Absolute:ScaleType::Source;}else if(base=="scale_type_x"){auto t=lower(v);a[i].pass.scale_type_x=t=="viewport"?ScaleType::Viewport:t=="absolute"?ScaleType::Absolute:ScaleType::Source;}else if(base=="scale_type_y"){auto t=lower(v);a[i].pass.scale_type_y=t=="viewport"?ScaleType::Viewport:t=="absolute"?ScaleType::Absolute:ScaleType::Source;}}
if(n<0)
    n=(int)a.size();
if(n<=0)
{
    e="glslp has no shaders: "+p;
    return false;
}
out.clear();
for(int i=0;i<n;i++)
{
    if(i>=(int)a.size()||a[i].path.empty())
    {
        e="missing shader"+std::to_string(i)+" in "+p;
        return false;
    }
    a[i].pass.source_path=a[i].path;
    out.push_back(a[i].pass);
}
return true;}
bool ShaderPipelineV2::load(const std::string&p,std::string&e){clear();if(has_suffix(p,".glslp")){std::vector<Pass>ps;if(!parse_glslp(p,ps,e))return false;for(auto&x:ps)if(!add_pass(x.source_path,&x,e)){clear();return false;}}else if(has_suffix(p,".agschain")){std::vector<std::string>ps;if(!parse_chain(p,ps,e))return false;for(auto&x:ps)if(!add_pass(x,nullptr,e)){clear();return false;}}else if(!add_pass(p,nullptr,e)){clear();return false;}return true;}
bool ShaderPipelineV2::ensure_target(Target&t,int w,int h,bool linear,std::string&e){if(!load_fbo_functions(e))return false;if(t.fbo&&t.width==w&&t.height==h){glBindTexture(GL_TEXTURE_2D,t.texture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,linear?GL_LINEAR:GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,linear?GL_LINEAR:GL_NEAREST);return true;}destroy_target(t);t.width=w;t.height=h;glGenTextures(1,&t.texture);glBindTexture(GL_TEXTURE_2D,t.texture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,linear?GL_LINEAR:GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,linear?GL_LINEAR:GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);pGenFramebuffers(1,&t.fbo);pBindFramebuffer(GL_FRAMEBUFFER_EXT,t.fbo);pFramebufferTexture2D(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,t.texture,0);if(pCheckFramebufferStatus(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT){e="incomplete shader framebuffer";destroy_target(t);return false;}return true;}
void ShaderPipelineV2::apply(unsigned input_texture,int iw,int ih,int ow,int oh){if(_passes.empty()||!input_texture)return;unsigned tex=input_texture;int sw=iw,sh=ih;glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);for(size_t i=0;i<_passes.size();++i){const Pass&p=_passes[i];bool last=i+1==_passes.size();int w=ow,h=oh;if(!last){w=resolve_dimension(p.scale_type_x,p.scale_x,sw,ow);h=resolve_dimension(p.scale_type_y,p.scale_y,sh,oh);}unsigned fbo=0;if(!last){std::string e;if(!ensure_target(_targets[i&1],w,h,p.filter_linear,e)){std::fprintf(stderr,"AGS shader: %s\n",e.c_str());break;}fbo=_targets[i&1].fbo;}if(fbo){pBindFramebuffer(GL_FRAMEBUFFER_EXT,fbo);}else if(pBindFramebuffer){pBindFramebuffer(GL_FRAMEBUFFER_EXT,0);}glViewport(0,0,w,h);glUseProgram(p.program);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,tex);if(p.texture>=0)glUniform1i(p.texture,0);if(p.input_size>=0)glUniform2f(p.input_size,(float)sw,(float)sh);if(p.texture_size>=0)glUniform2f(p.texture_size,(float)sw,(float)sh);if(p.output_size>=0)glUniform2f(p.output_size,(float)w,(float)h);if(p.frame_count>=0)glUniform1i(p.frame_count,(GLint)_frame_count);if(p.frame_direction>=0)glUniform1i(p.frame_direction,1);glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(V),&quad[0].x);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(V),&quad[0].u);glDrawArrays(GL_TRIANGLE_STRIP,0,4);glDisableVertexAttribArray(0);glDisableVertexAttribArray(1);if(!last){tex=_targets[i&1].texture;sw=w;sh=h;}}glUseProgram(0);glBindTexture(GL_TEXTURE_2D,0);if(pBindFramebuffer)pBindFramebuffer(GL_FRAMEBUFFER_EXT,0);++_frame_count;}
