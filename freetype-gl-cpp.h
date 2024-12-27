#include <stdarg.h>
#include <stdio.h>
#include <string>

#ifdef WITH_EIGEN
#include <eigen3/Eigen/Dense>
#endif

#include "freetype-gl/freetype-gl.h"
#include "freetype-gl/font-manager.h"
#include "freetype-gl/vertex-buffer.h"
#include "freetype-gl/text-buffer.h"
#include "freetype-gl/markup.h"
#include "freetype-gl/demos/mat4.h"

namespace ftgl {

    const char* shader_text_frag = R"(

    /* Freetype GL - A C OpenGL Freetype engine
 *
 * Distributed under the OSI-approved BSD 2-Clause License.  See accompanying
 * file `LICENSE` for more details.
 */
vec3
energy_distribution( vec4 previous, vec4 current, vec4 next )
{
    float primary   = 1.0/3.0;
    float secondary = 1.0/3.0;
    float tertiary  = 0.0;

    // Energy distribution as explained on:
    // http://www.grc.com/freeandclear.htm
    //
    //  .. v..
    // RGB RGB RGB
    // previous.g + previous.b + current.r + current.g + current.b
    //
    //   . .v. .
    // RGB RGB RGB
    // previous.b + current.r + current.g + current.b + next.r
    //
    //     ..v ..
    // RGB RGB RGB
    // current.r + current.g + current.b + next.r + next.g

    float r =
        tertiary  * previous.g +
        secondary * previous.b +
        primary   * current.r  +
        secondary * current.g  +
        tertiary  * current.b;

    float g =
        tertiary  * previous.b +
        secondary * current.r +
        primary   * current.g  +
        secondary * current.b  +
        tertiary  * next.r;

    float b =
        tertiary  * current.r +
        secondary * current.g +
        primary   * current.b +
        secondary * next.r    +
        tertiary  * next.g;

    return vec3(r,g,b);
}

uniform sampler2D tex;
uniform vec3 pixel;

varying vec4 vcolor;
varying vec2 vtex_coord;
varying float vshift;
varying float vgamma;

void main()
{
    // LCD Off
    if( pixel.z == 1.0)
    {
        float a = texture2D(tex, vtex_coord).r;
        gl_FragColor = vcolor * pow( a, 1.0/vgamma );
        return;
    }

    // LCD On
    vec4 current = texture2D(tex, vtex_coord);
    vec4 previous= texture2D(tex, vtex_coord+vec2(-1.,0.)*pixel.xy);
    vec4 next    = texture2D(tex, vtex_coord+vec2(+1.,0.)*pixel.xy);

    current = pow(current, vec4(1.0/vgamma));
    previous= pow(previous, vec4(1.0/vgamma));

    float r = current.r;
    float g = current.g;
    float b = current.b;

    if( vshift <= 0.333 )
    {
        float z = vshift/0.333;
        r = mix(current.r, previous.b, z);
        g = mix(current.g, current.r,  z);
        b = mix(current.b, current.g,  z);
    }
    else if( vshift <= 0.666 )
    {
        float z = (vshift-0.33)/0.333;
        r = mix(previous.b, previous.g, z);
        g = mix(current.r,  previous.b, z);
        b = mix(current.g,  current.r,  z);
    }
   else if( vshift < 1.0 )
    {
        float z = (vshift-0.66)/0.334;
        r = mix(previous.g, previous.r, z);
        g = mix(previous.b, previous.g, z);
        b = mix(current.r,  previous.b, z);
    }

   float t = max(max(r,g),b);
   vec4 color = vec4(vcolor.rgb, (r+g+b)/3.0);
   color = t*color + (1.0-t)*vec4(r,g,b, min(min(r,g),b));
   gl_FragColor = vec4( color.rgb, vcolor.a*color.a);


//    gl_FragColor = vec4(pow(vec3(r,g,b),vec3(1.0/vgamma)),a);

    /*
    vec3 color = energy_distribution(previous, vec4(r,g,b,1), next);
    color = pow( color, vec3(1.0/vgamma));

    vec3 color = vec3(r,g,b); //pow( vec3(r,g,b), vec3(1.0/vgamma));
    gl_FragColor.rgb = color;
    gl_FragColor.a = (color.r+color.g+color.b)/3.0 * vcolor.a;
    */

//    gl_FragColor = vec4(pow(vec3(r,g,b),vec3(1.0/vgamma)),a);
    //gl_FragColor = vec4(r,g,b,a);
}


)";

    const char* shader_text_vert = R"(

    /* Freetype GL - A C OpenGL Freetype engine
 *
 * Distributed under the OSI-approved BSD 2-Clause License.  See accompanying
 * file `LICENSE` for more details.
 */
    uniform sampler2D tex;
uniform vec3 pixel;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

attribute vec3 vertex;
attribute vec4 color;
attribute vec2 tex_coord;
attribute float ashift;
attribute float agamma;

varying vec4 vcolor;
varying vec2 vtex_coord;
varying float vshift;
varying float vgamma;

void main()
{
    vshift = ashift;
    vgamma = agamma;
    vcolor = color;
    vtex_coord = tex_coord;
    gl_Position = projection * (view * (model * vec4(vertex, 1.0)));
}

)";

#ifdef WITH_EIGEN
void eigen2mat4(const Eigen::Matrix4f& src, ftgl::mat4* dst);
#endif

class FreetypeGl;

class Markup {
friend class FreetypeGl;
public:
    Markup();
    Markup(const std::string& font_family,
           float size,
           const vec4 &color,
           bool bold,
           bool underlined,
           bool italic,
           bool strikethrough,
           bool overline,
           FreetypeGl* freetype_gl);

    Markup(Markup&& other);
    Markup(const Markup& other) = delete;
    Markup& operator=(Markup&& other);
    Markup& operator=(const Markup& other) = delete;
    virtual ~Markup();

    markup_t description;
private:
    FreetypeGl* manager;
};

class FreetypeGlText {
public:
    template <typename... markup_text>
    explicit FreetypeGlText(const FreetypeGl* freetypeGL, const markup_text&... content) : manager(freetypeGL)
    {
        text_buffer = text_buffer_new();
        vec2 pen = {{0,0}};

        text_buffer_printf(text_buffer, &pen, content...);
        mat4_set_identity(&pose);
    }

    FreetypeGlText(FreetypeGlText&& other);
    FreetypeGlText(const FreetypeGlText& other) = delete;
    FreetypeGlText& operator=(const FreetypeGlText& other) = delete;

    virtual ~FreetypeGlText();

    inline const text_buffer_t* getTextBuffer() const { return text_buffer; }

    void render();

#ifdef WITH_EIGEN
    void setPose(const Eigen::Matrix4f& pose);
    void setPosition(const Eigen::Vector3f& position);
#endif
    inline void setPose(const mat4& p){ pose = p; }
    inline void setPosition(float x, float y, float z) { pose.m30 = x; pose.m31 = y; pose.m32 = z;}
    ftgl::mat4 pose;

    inline void setScalingFactor(float s){ scaling_factor = s; }
    ftgl::mat4 getModelMatrix() const;

private:
    float scaling_factor = 1;

    const FreetypeGl* manager;
    text_buffer_t* text_buffer;
};


class FreetypeGl {
friend class Markup;
public:
    FreetypeGl(bool initialise = false);
    FreetypeGl(const FreetypeGl& other) = delete;
    FreetypeGl& operator=(const FreetypeGl& other) = delete;
    ~FreetypeGl();

    void init();

    Markup createMarkup(const std::string& font_family,
                          float size,
                          const vec4& color=FreetypeGl::COLOR_WHITE,
                          bool bold=false,
                          bool underlined=false,
                          bool italic=false,
                          bool strikethrough=false,
                          bool overline=false);

    static std::string findFont(const std::string& search_pattern);

    FreetypeGlText createText(const std::string& text, const Markup& markup);
    FreetypeGlText createText(const std::string& text, const markup_t* markup = NULL);

    /**
     * @brief renderText Renders text directly (slow but easy to use)
     * @param text
     */
    void renderText(const std::string& text);

    void renderText(const FreetypeGlText& text, bool call_pre_post=true) const;

    void updateTexture();

    void preRender() const;
    void postRender() const;

#ifdef WITH_EIGEN
    void setView(const Eigen::Matrix4f& v);
    void setProjection(const Eigen::Matrix4f& p);
#endif
    void setView(const ftgl::mat4& v);
    void setProjection(const ftgl::mat4& p);
    void setView(const float* v);
    void setProjection(const float* p);
    void setProjectionOrtho(float left,   float right,
                                   float bottom, float top,
                                   float znear,  float zfar);
    void setProjectionPresp(float fovy,   float aspect,
                                   float znear,  float zfar);

    constexpr static vec4 COLOR_BLACK  = {{0.0, 0.0, 0.0, 1.0}};
    constexpr static vec4 COLOR_WHITE  = {{1.0, 1.0, 1.0, 1.0}};
    constexpr static vec4 COLOR_RED = {{1.0, 0.0, 0.0, 1.0}};
    constexpr static vec4 COLOR_GREEN = {{0.0, 1.0, 0.0, 1.0}};
    constexpr static vec4 COLOR_BLUE = {{0.0, 0.0, 1.0, 1.0}};
    constexpr static vec4 COLOR_YELLOW = {{1.0, 1.0, 0.0, 1.0}};
    constexpr static vec4 COLOR_GREY   = {{0.5, 0.5, 0.5, 1.0}};
    constexpr static vec4 COLOR_NONE   = {{1.0, 1.0, 1.0, 0.0}};
    constexpr static mat4 identity = {{1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0}};

    mat4 view;
    mat4 projection;

private:

    GLuint compileShader(const char* source, const GLenum type);
    GLuint loadShader(const char* frag, const char* vert);

    void addLatin1Alphabet();

    GLuint text_shader = 0;
    font_manager_t* font_manager;
    Markup default_markup;
};


}
