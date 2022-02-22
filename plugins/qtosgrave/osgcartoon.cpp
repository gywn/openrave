// -*- coding: utf-8 -*-
// openrave: tweaked openscenegraph cartoon shader

#include "osgcartoon.h"

#include <osgFX/Registry>

#include <osg/PolygonOffset>
#include <osg/Texture1D>
#include <osg/VertexProgram>
#include <osg/PolygonMode>
#include <osg/CullFace>
#include <osg/Image>
#include <osg/TexEnv>
#include <osg/LineWidth>
#include <osg/Material>
#include <osg/Program>
#include <osg/Shader>
#include <osg/BlendEquation>
#include <osg/BlendFunc>
#include <osg/Depth>


#include <sstream>

namespace qtosgrave {

using namespace osgFX;

// register a prototype for this effect
Registry::Proxy proxy(new OpenRAVECartoon);

class OGLSL_Technique : public Technique {
public:
    OGLSL_Technique(osg::Material* wf_mat, osg::LineWidth *wf_lw, int lightnum)
        : Technique(), _wf_mat(wf_mat), _wf_lw(wf_lw), _lightnum(lightnum) {
    }

    void getRequiredExtensions(std::vector<std::string>& extensions) const override
    {
        extensions.emplace_back("GL_ARB_shader_objects");
        extensions.emplace_back("GL_ARB_vertex_shader");
        extensions.emplace_back("GL_ARB_fragment_shader");
    }

protected:

    void define_passes() override
    {
        // Pass #1 (Paint objects)
        {
            osg::ref_ptr<osg::StateSet> ss = new osg::StateSet;

            const auto vert_source = R"(
                varying float frag_normal_intensity;
                void main(void)
                {
                    vec4 light_dir_in_eye;
                    if (gl_ProjectionMatrix[3][3] == 0) {
                        // Perspective Projection: Lights come from the camera
                        light_dir_in_eye = normalize(gl_ModelViewMatrix * gl_Vertex);
                    } else {
                        // Orthographic Projection: Lights come from infinitely far away
                        light_dir_in_eye = vec4(0.0, 0.0, -1.0, 0.0);
                    }
                    vec3 vertex_normal_in_eye = normalize(gl_NormalMatrix * gl_Normal);
                    frag_normal_intensity = pow(abs(dot(-light_dir_in_eye, vertex_normal_in_eye)), 0.25);
                    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
                }
                )";

            const auto frag_source = R"(
                varying float frag_normal_intensity;
                void main(void)
                {
                    if (gl_FrontMaterial.diffuse.w == 1.0) {
                        gl_FragColor.xyz = frag_normal_intensity * gl_FrontMaterial.diffuse.xyz + gl_FrontMaterial.ambient.xyz;
                    } else {
                        gl_FragColor.xyz = 1.0 - gl_FrontMaterial.diffuse.xyz - gl_FrontMaterial.ambient.xyz;
                    }
                    gl_FragColor.w = pow(mix(1.0, gl_FrontMaterial.diffuse.w, frag_normal_intensity), 2.5);
                }
            )";

            osg::ref_ptr<osg::Program> program = new osg::Program;
            program->addShader(osg::ref_ptr<osg::Shader>(new osg::Shader(osg::Shader::VERTEX, vert_source)));
            program->addShader(osg::ref_ptr<osg::Shader>(new osg::Shader(osg::Shader::FRAGMENT, frag_source)));
            ss->setAttributeAndModes( program.get(), osg::StateAttribute::OVERRIDE | osg::StateAttribute::ON);

            addPass(ss.get());
        }

        // Pass #2 (Rebuild Z-buffer)
        {
            osg::ref_ptr<osg::StateSet> ss = new osg::StateSet;

            ss->setAttributeAndModes(osg::ref_ptr<osg::ColorMask>(new osg::ColorMask(false, false, false, false)));
            ss->setAttributeAndModes(osg::ref_ptr<osg::Depth>( new osg::Depth(osg::Depth::LESS, 0, 1, true)), osg::StateAttribute::OVERRIDE | osg::StateAttribute::ON);

            const char * frag_source_2 = R"(
                void main(void)
                {
                    float delta = -1e-3; // 2mm
                    if (gl_ProjectionMatrix[3][3] == 0) {
                        // Perspective Projection
                        gl_FragDepth = gl_FragCoord.z - delta * (gl_ProjectionMatrix[2][2] + gl_FragCoord.z) * (gl_ProjectionMatrix[2][2] + gl_FragCoord.z) / gl_ProjectionMatrix[3][2];
                    } else {
                        // Orthographic Projection
                        gl_FragDepth = gl_FragCoord.z - delta * gl_ProjectionMatrix[2][2];
                    }
                };
                )";

            osg::ref_ptr<osg::Program> program = new osg::Program;
            program->addShader(osg::ref_ptr<osg::Shader>(new osg::Shader(osg::Shader::FRAGMENT, frag_source_2 )));
            ss->setAttributeAndModes(program.get(), osg::StateAttribute::OVERRIDE | osg::StateAttribute::ON);

            addPass(ss.get());
        }

        // Pass #3 (Outline)
        {
            osg::ref_ptr<osg::StateSet> ss = new osg::StateSet;

            ss->setAttributeAndModes(osg::ref_ptr<osg::BlendEquation>(new osg::BlendEquation(osg::BlendEquation::FUNC_ADD)), osg::StateAttribute::OVERRIDE | osg::StateAttribute::ON);
            ss->setAttributeAndModes(osg::ref_ptr<osg::BlendFunc>(new osg::BlendFunc(osg::BlendFunc::ONE, osg::BlendFunc::ZERO)), osg::StateAttribute::OVERRIDE | osg::StateAttribute::ON);
            ss->setAttributeAndModes(osg::ref_ptr<osg::CullFace>(new osg::CullFace(osg::CullFace::FRONT)), osg::StateAttribute::OVERRIDE | osg::StateAttribute::ON);
            ss->setAttributeAndModes(osg::ref_ptr<osg::PolygonMode>(new osg::PolygonMode(osg::PolygonMode::FRONT_AND_BACK, osg::PolygonMode::LINE)), osg::StateAttribute::OVERRIDE | osg::StateAttribute::ON);
            ss->setAttributeAndModes(_wf_mat, osg::StateAttribute::OVERRIDE | osg::StateAttribute::ON);
            ss->setAttributeAndModes(_wf_lw, osg::StateAttribute::OVERRIDE | osg::StateAttribute::ON);

            addPass(ss.get());
        }
    }

private:
    osg::ref_ptr<osg::Material> _wf_mat;
    osg::ref_ptr<osg::LineWidth> _wf_lw;
    int _lightnum;
};

OpenRAVECartoon::OpenRAVECartoon()
    :    Effect(),
    _wf_mat(new osg::Material),
    _wf_lw(new osg::LineWidth(4.0f)),
    _lightnum(0)
{
    _wf_mat->setColorMode(osg::Material::OFF);
    _wf_mat->setDiffuse(osg::Material::FRONT_AND_BACK, osg::Vec4(0.0f, 0.0f, 0.0f, 1.0f));
    _wf_mat->setAmbient(osg::Material::FRONT_AND_BACK, osg::Vec4(0.0f, 0.0f, 0.0f, 1.0f));
    _wf_mat->setSpecular(osg::Material::FRONT_AND_BACK, osg::Vec4(0.0f, 0.0f, 0.0f, 1.0f));
    _wf_mat->setEmission(osg::Material::FRONT_AND_BACK, osg::Vec4(0.0f,0.0f,0.0f,1.0f));
}

OpenRAVECartoon::OpenRAVECartoon(const OpenRAVECartoon& copy, const osg::CopyOp& copyop)
    :    Effect(copy, copyop),
    _wf_mat(static_cast<osg::Material* >(copyop(copy._wf_mat.get()))),
    _wf_lw(static_cast<osg::LineWidth *>(copyop(copy._wf_lw.get()))),
    _lightnum(copy._lightnum)
{
}

bool OpenRAVECartoon::define_techniques()
{
    addTechnique(new OGLSL_Technique(_wf_mat.get(), _wf_lw.get(), _lightnum));
    return true;
}

} // end namespace qtosgrave
