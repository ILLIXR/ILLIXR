#include "common/threadloop.hpp"
#include "common/plugin.hpp"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <mxre>
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/pose_prediction.hpp"
#include "common/extended_window.hpp"
#include "common/math_util.hpp"
#include "common/phonebook.hpp"

using namespace ILLIXR;

class mxre_reader : public threadloop {
  public:
    mxre_reader(std::string name_, phonebook* pb_)
      : threadloop{name_, pb_}
      , sb{pb->lookup_impl<switchboard>()}
      , xwin{new xlib_gl_extended_window{1, 1, pb->lookup_impl<xlib_gl_extended_window>()->glc}}
      , _m_eyebuffer{sb->publish<rendered_frame>("eyebuffer")}
      , pp{pb->lookup_impl<pose_prediction>()}
      //, _m_mxre_frame{sb->publish<imu_cam_type>("mxre_frame")}
    {}

    virtual void _p_thread_setup() {
      // Init reader connection to MXRE here
      illixrSink.setup("sink", MX_DTYPE_CVMAT);

      glXMakeCurrent(xwin->dpy, xwin->win, xwin->glc);
    }

    virtual void _p_one_iteration() {
      const fast_pose_type fast_pose = pp->get_fast_pose();
      // Poll MXRE for new frame, If there is a new frame, publish it to the plug
      illixrSink.recv(&recvFrame);
      cv::flip(recvFrame, recvFrame, 0);
      mxre::cv_types::Mat textureMat(recvFrame);
      if(glIsTexture(texture_handle))
        mxre::gl_utils::updateTextureFromCVFrame(textureMat, texture_handle);
      else
        mxre::gl_utils::makeTextureFromCVFrame(textureMat, texture_handle);
      recvFrame.release();
      textureMat.release();

      for(int eye_idx = 0; eye_idx < 2; eye_idx++) {
        glBindTexture(GL_TEXTURE_2D, eyeTextures[eye_idx]);
        glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, eyeTextures[eye_idx], 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glClearColor(0.9f, 0.9f, 0.9f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        mxre::gl_utils::startBackground(1024, 1024);
        glBegin(GL_QUADS);
        glColor3f(1, 1, 1);
        glTexCoord2i(0,0); glVertex3f(0,       0, -1);
        glTexCoord2i(1,0); glVertex3f(1024,    0, -1);
        glTexCoord2i(1,1); glVertex3f(1024, 1024, -1);
        glTexCoord2i(0,1); glVertex3f(0,    1024, -1);
        glEnd();
        mxre::gl_utils::endBackground();
      }


			// Publish our submitted frame handle to Switchboard!
      auto frame = new rendered_frame;
      frame->texture_handles[0] = texture_handle;
      frame->texture_handles[1] = texture_handle;
      frame->swap_indices[0] = 0;
      frame->swap_indices[1] = 0;

      frame->render_pose = fast_pose;
      //which_buffer.store(buffer_to_use == 1 ? 0 : 1);
      frame->render_time = std::chrono::high_resolution_clock::now();
      _m_eyebuffer->put(frame);
      //lastFrameTime = std::chrono::high_resolution_clock::now();

      //_m_mxre_frame->put(&recvFrame);
    }

    virtual void start() override {
      glXMakeCurrent(xwin->dpy, xwin->win, xwin->glc);
      // Initialize the GLFW library, still need it to get time
      if(!glfwInit()){
        printf("Failed to initialize glfw\n");
      }
      // Init and verify GLEW
      if(glewInit()){
        printf("Failed to init GLEW\n");
        exit(0);
      }

      createSharedEyebuffer(&eyeTextures[0]);
      createSharedEyebuffer(&eyeTextures[1]);
      createFBO(&eyeTextures[0], &eyeTextureFBO, &eyeTextureDepthTarget);
      math_util::projection_fov( &basicProjection, 40.0f, 40.0f, 40.0f, 40.0f, 0.03f, 20.0f );

      glXMakeCurrent(xwin->dpy, None, NULL);

      threadloop::start();
    }

  private:
    const std::unique_ptr<const xlib_gl_extended_window> xwin;
    const std::shared_ptr<switchboard> sb;
    const std::shared_ptr<pose_prediction> pp;

    std::unique_ptr<writer<rendered_frame>> _m_eyebuffer;
    GLuint eyeTextures[2];
    GLuint eyeTextureFBO;
    GLuint eyeTextureDepthTarget;
    Eigen::Matrix4f basicProjection;

    //std::unique_ptr<writer<imu_cam_type>> _m_mxre_frame;
    //std::unique_ptr<writer<int>> _m_mxre_frame;
    mxre::types::ILLIXRSink<cv::Mat> illixrSink;
    cv::Mat recvFrame;
    GLuint texture_handle;

    int createSharedEyebuffer(GLuint* texture_handle){

      // Create the shared eye texture handle.
      glGenTextures(1, texture_handle);
      glBindTexture(GL_TEXTURE_2D, *texture_handle);

      // Set the texture parameters for the texture that the FBO will be
      // mapped into.
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 1024, 1024, 0, GL_RGB, GL_UNSIGNED_BYTE, 0);

      glBindTexture(GL_TEXTURE_2D, 0); // unbind texture, will rebind later

      if(glGetError()){
        return 0;
      } else {
        return 1;
      }
    }

    void createFBO(GLuint* texture_handle, GLuint* fbo, GLuint* depth_target){
      // Create a framebuffer to draw some things to the eye texture
      glGenFramebuffers(1, fbo);
      // Bind the FBO as the active framebuffer.
      glBindFramebuffer(GL_FRAMEBUFFER, *fbo);

      glGenRenderbuffers(1, depth_target);
      glBindRenderbuffer(GL_RENDERBUFFER, *depth_target);
      glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, 1024, 1024);
      glBindRenderbuffer(GL_RENDERBUFFER, 0);

      // Bind eyebuffer texture
      printf("About to bind eyebuffer texture, texture handle: %d\n", *texture_handle);

      glBindTexture(GL_TEXTURE_2D, *texture_handle);
      glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, *texture_handle, 0);
      glBindTexture(GL_TEXTURE_2D, 0);
      // attach a renderbuffer to depth attachment point
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, *depth_target);

      if(glGetError()){
        printf("displayCB, error after creating fbo\n");
      }

      // Unbind FBO.
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }


};

PLUGIN_MAIN(mxre_reader)
