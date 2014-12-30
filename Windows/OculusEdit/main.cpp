//
//  main.cpp
//  hello_opengl_oculus
//
//  Created by James Hagerman on 12/26/14.
//  Copyright (c) 2014 James Hagerman. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>
#include <string>

#if !defined(__APPLE__)
#include <GL/glew.h>
#endif

#if defined(__APPLE__)
//#include <OpenGL/gl3.h>
#endif

#if defined(_WIN32)
//#  include <Windows.h>
//#  include <algorithm>
// If you don't include these in the Visual Studio project configs. These need to go before OVR_CAPI.h:
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#  define GLFW_EXPOSE_NATIVE_WIN32
#  define GLFW_EXPOSE_NATIVE_WGL
#elif defined(__APPLE__)
#  define GLFW_INCLUDE_GLCOREARB
#elif defined(__linux__)
#  include <X11/X.h>
#  include <X11/extensions/Xrandr.h>
#  define GLFW_EXPOSE_NATIVE_X11
#  define GLFW_EXPOSE_NATIVE_GLX
#endif

#include <GLFW/glfw3.h>
#if !defined(__APPLE__)
#  include <GLFW/glfw3native.h>
#endif


#include <glm/glm.hpp>

#include "OVR.h"
#include "OVR_CAPI.h"
#include "OVR_CAPI_GL.h"
//#include "Kernel\OVR_Math.h"
//#include "Kernel\OVR_TYPES.h"

using namespace OVR;

// Requred variables:
ovrHmd hmd;
bool directHmdMode = true;
ovrSizei resolution;
ovrVector2i position;
GLFWwindow* l_Window;
GLFWmonitor* l_Monitor;
ovrSizei l_ClientSize;


// More variables taken from the single page OpenGL demo:
int g_DistortionCaps = 0
| ovrDistortionCap_Vignette
| ovrDistortionCap_Chromatic
| ovrDistortionCap_Overdrive
| ovrDistortionCap_TimeWarp // Turning this on gives ghosting???
;

ovrGLConfig g_Cfg;
ovrEyeRenderDesc g_EyeRenderDesc[2];
ovrVector3f g_EyeOffsets[2];
ovrPosef g_EyePoses[2];
ovrTexture g_EyeTextures[2];
OVR::Matrix4f g_ProjectionMatrici[2];
OVR::Sizei g_RenderTargetSize;
ovrVector3f g_CameraPosition;

// The OpenGL Shader Program variables:
GLuint theProgram; // The program itself
GLuint positionBufferObject; // The position buffer object
GLuint elapsedTimeUniform;
GLuint loopDuration; // We will set this only once when the program is finished linking
GLuint vao; // the vertex array

// The shaders themselves:
const std::string strVertexShader(
	"#version 330\n"
	"layout (location = 0) in vec4 position;\n"
	"layout (location = 1) in vec4 color;\n"
	"smooth out vec4 theColor;\n"
	"uniform float loopDuration;\n"
	"uniform float time;\n"
	"void main()\n"
	"{\n"
	"   float timeScale = 3.14159f * 2.0f / loopDuration;\n"
	"   float currTime = mod(time, loopDuration);\n"
	"   vec4 totalOffset = vec4(cos(currTime * timeScale) * 0.5f, sin(currTime *timeScale) * 0.5f, 0.0, 0.0);\n"
	"   gl_Position = position + totalOffset;\n"
	"   theColor = color;\n"
	"}\n"
	);

const std::string strFragmentShader(
	"#version 330\n"
	"smooth in vec4 theColor;\n"
	"out vec4 outputColor;\n"
	"void main()\n"
	"{\n"
	//"//   outputColor = vec4(1.0f, 1.0f, 1.0f, 1.0f);\n"
	"   outputColor = theColor;\n"
	"}\n"
	);

// Shader program builder functions:
GLuint CreateShader(GLenum eShaderType, const std::string &strShaderFile)
{
	GLuint shader = glCreateShader(eShaderType);
	const char *strFileData = strShaderFile.c_str();
	glShaderSource(shader, 1, &strFileData, NULL);

	glCompileShader(shader);

	GLint status;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (status == GL_FALSE)
	{
		GLint infoLogLength;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);

		GLchar *strInfoLog = new GLchar[infoLogLength + 1];
		glGetShaderInfoLog(shader, infoLogLength, NULL, strInfoLog);

		const char *strShaderType = NULL;
		switch (eShaderType)
		{
		case GL_VERTEX_SHADER: strShaderType = "vertex"; break;
		case GL_GEOMETRY_SHADER: strShaderType = "geometry"; break;
		case GL_FRAGMENT_SHADER: strShaderType = "fragment"; break;
		}
		printf("JDB: oops! shader compile issue...\n");
		fprintf(stderr, "Compile failure in %s shader:\n%s\n", strShaderType, strInfoLog);
		delete[] strInfoLog;
	}

	return shader;
}

GLuint CreateProgram(const std::vector<GLuint> &shaderList)
{
	GLuint program = glCreateProgram();

	for (size_t iLoop = 0; iLoop < shaderList.size(); iLoop++)
		glAttachShader(program, shaderList[iLoop]);

	glLinkProgram(program);

	GLint status;
	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (status == GL_FALSE)
	{
		GLint infoLogLength;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogLength);

		GLchar *strInfoLog = new GLchar[infoLogLength + 1];
		glGetProgramInfoLog(program, infoLogLength, NULL, strInfoLog);
		printf("JDB: oops! shader program linker issue\n");
		fprintf(stderr, "Linker failure: %s\n", strInfoLog);
		delete[] strInfoLog;
	}

	for (size_t iLoop = 0; iLoop < shaderList.size(); iLoop++)
		glDetachShader(program, shaderList[iLoop]);

	return program;
}

void InitializeProgram()
{
	std::vector<GLuint> shaderList;

	shaderList.push_back(CreateShader(GL_VERTEX_SHADER, strVertexShader));
	shaderList.push_back(CreateShader(GL_FRAGMENT_SHADER, strFragmentShader));

	theProgram = CreateProgram(shaderList);

	std::for_each(shaderList.begin(), shaderList.end(), glDeleteShader);

	
}

// Vertex array for use with shader program:
const float vertexPositions[] = {
	0.75f, 0.75f, 0.0f, 1.0f, 
	0.75f, -0.75f, 0.0f, 1.0f, 
	-0.75f, -0.75f, 0.0f, 1.0f, 
	1.0f, 0.0f, 0.0f, 1.0f,
	0.0f, 1.0f, 0.0f, 1.0f,
	0.0f, 0.0f, 1.0f, 1.0f
};

// Old OpenGL Scene object:
GLfloat l_VAPoints[] =
{
	0.5f, 0.5f, 0.5f,
	-0.5f, 0.5f, 0.5f,
	-0.5f, -0.5f, 0.5f,
	0.5f, -0.5f, 0.5f,
	-0.5f, -0.5f, -0.5f,
	-0.5f, 0.5f, -0.5f,
	0.5f, 0.5f, -0.5f,
	0.5f, -0.5f, -0.5f,
	0.5f, 0.5f, 0.5f,
	0.5f, 0.5f, -0.5f,
	-0.5f, 0.5f, -0.5f,
	-0.5f, 0.5f, 0.5f,
	-0.5f, -0.5f, -0.5f,
	0.5f, -0.5f, -0.5f,
	0.5f, -0.5f, 0.5f,
	-0.5f, -0.5f, 0.5f,
	0.5f, 0.5f, 0.5f,
	0.5f, -0.5f, 0.5f,
	0.5f, -0.5f, -0.5f,
	0.5f, 0.5f, -0.5f,
	-0.5f, -0.5f, -0.5f,
	-0.5f, -0.5f, 0.5f,
	-0.5f, 0.5f, 0.5f,
	-0.5f, 0.5f, -0.5f
};

GLfloat l_VANormals[] =
{
	0.0f, 0.0f, 1.0f,
	0.0f, 0.0f, 1.0f,
	0.0f, 0.0f, 1.0f,
	0.0f, 0.0f, 1.0f,
	0.0f, 0.0f, -1.0f,
	0.0f, 0.0f, -1.0f,
	0.0f, 0.0f, -1.0f,
	0.0f, 0.0f, -1.0f,
	0.0f, 1.0f, 0.0f,
	0.0f, 1.0f, 0.0f,
	0.0f, 1.0f, 0.0f,
	0.0f, 1.0f, 0.0f,
	0.0f, -1.0f, 0.0f,
	0.0f, -1.0f, 0.0f,
	0.0f, -1.0f, 0.0f,
	0.0f, -1.0f, 0.0f,
	1.0f, 0.0f, 0.0f,
	1.0f, 0.0f, 0.0f,
	1.0f, 0.0f, 0.0f,
	1.0f, 0.0f, 0.0f,
	-1.0f, 0.0f, 0.0f,
	-1.0f, 0.0f, 0.0f,
	-1.0f, 0.0f, 0.0f,
	-1.0f, 0.0f, 0.0f
};

GLuint l_VAIndici[] =
{
	0, 1, 2, 3,
	4, 5, 6, 7,
	8, 9, 10, 11,
	12, 13, 14, 15,
	16, 17, 18, 19,
	20, 21, 22, 23
};

// Vertex array initializer functions:
void InitializeVertexBuffer()
{
	// For use with the new shader version of this code
	glGenBuffers(1, &positionBufferObject);

	glBindBuffer(GL_ARRAY_BUFFER, positionBufferObject);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertexPositions), vertexPositions, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void RenderCubeVertexArrays(void)
{
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(3, GL_FLOAT, 0, l_VAPoints);

	glEnableClientState(GL_NORMAL_ARRAY);
	glNormalPointer(GL_FLOAT, 0, l_VANormals);

	glDrawElements(GL_QUADS, 6 * 4, GL_UNSIGNED_INT, l_VAIndici);

	glDisableClientState(GL_NORMAL_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
}

static void SetStaticLightPositions(void)
{
	GLfloat l_Light0Position[] = { 3.0f, 4.0f, 2.0f, 0.0f };
	glLightfv(GL_LIGHT0, GL_POSITION, l_Light0Position);

	GLfloat l_Light1Position[] = { -3.0f, -4.0f, 2.0f, 0.0f };
	glLightfv(GL_LIGHT1, GL_POSITION, l_Light1Position);
}

// Taken from the one page opengl demo:
const bool l_MultiSampling = false;
static void SetOpenGLState(void)
{
	// Some state...
	//glEnable(GL_CULL_FACE); // Disable until out triangles are ordered correctly...
	glEnable(GL_LIGHTING);
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_DEPTH_TEST);
	glShadeModel(GL_SMOOTH);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	if (l_MultiSampling) glEnable(GL_MULTISAMPLE); else glDisable(GL_MULTISAMPLE);
	glClearColor(0.2f, 0.3f, 0.4f, 1.0f);

	// Material...
	GLfloat l_MaterialSpecular[] = { 0.3f, 0.3f, 0.3f, 1.0f };
	GLfloat l_MaterialShininess[] = { 10.0f };
	glMaterialfv(GL_FRONT, GL_SPECULAR, l_MaterialSpecular);
	glMaterialfv(GL_FRONT, GL_SHININESS, l_MaterialShininess);

	// Some (stationary) lights, position will be set every frame separately...
	GLfloat l_Light0Diffuse[] = { 1.0f, 0.8f, 0.6f, 1.0f };
	glLightfv(GL_LIGHT0, GL_DIFFUSE, l_Light0Diffuse);
	glEnable(GL_LIGHT0);

	GLfloat l_Light1Diffuse[] = { 0.6f, 0.8f, 1.0f, 1.0f };
	glLightfv(GL_LIGHT1, GL_DIFFUSE, l_Light1Diffuse);
	glEnable(GL_LIGHT1);
}

static void ErrorCallback(int p_Error, const char* p_Description)
{
	printf("ERROR: %d, %s\n", p_Error, p_Description);
}

void printGLContextInfo(GLFWwindow* pW)
{
	// Print some info about the OpenGL context...
	const int l_Major = glfwGetWindowAttrib(pW, GLFW_CONTEXT_VERSION_MAJOR);
	const int l_Minor = glfwGetWindowAttrib(pW, GLFW_CONTEXT_VERSION_MINOR);
	const int l_Profile = glfwGetWindowAttrib(pW, GLFW_OPENGL_PROFILE);
	if (l_Major >= 3) // Profiles introduced in OpenGL 3.0...
	{
		if (l_Profile == GLFW_OPENGL_COMPAT_PROFILE)
		{
			printf("GLFW_OPENGL_COMPAT_PROFILE\n");
		}
		else
		{
			printf("GLFW_OPENGL_CORE_PROFILE\n");
		}
	}
	printf("OpenGL: %d.%d ", l_Major, l_Minor);
	printf("Vendor: %s\n", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
	printf("Renderer: %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
}

static void WindowSizeCallback(GLFWwindow* p_Window, int p_Width, int p_Height)
{
	if (p_Width>0 && p_Height>0)
	{
		g_Cfg.OGL.Header.BackBufferSize.w = p_Width;
		g_Cfg.OGL.Header.BackBufferSize.h = p_Height;

		ovrBool l_ConfigureResult = ovrHmd_ConfigureRendering(hmd, &g_Cfg.Config, g_DistortionCaps, hmd->MaxEyeFov, g_EyeRenderDesc);
		glUseProgram(0); // Avoid OpenGL state leak in ovrHmd_ConfigureRendering...
		if (!l_ConfigureResult)
		{
			printf("Configure failed.\n");
			exit(EXIT_FAILURE);
		}
	}
}

void keyboard(GLFWwindow* pWindow, int key, int codes, int action, int mods)
{
	(void)pWindow;
	(void)codes;

	if ((key > -1) && (key <= GLFW_KEY_LAST))
	{
		printf("key ac  %d %d\n", key, action);
	}

	if (action == GLFW_PRESS)
	{
		switch (key)
		{
		default:
			ovrHmd_DismissHSWDisplay(hmd);
			break;

		case GLFW_KEY_ESCAPE:
			glfwSetWindowShouldClose(pWindow, GL_TRUE);
			//if (directHmdMode)
			//{
			//	// Clear the frame before calling all the destructors - even a few
			//	// frames worth of frozen video is enough to cause discomfort!
			//	///@note This does not seem to work in Direct mode.
			//	//glClearColor(58.f / 255.f, 110.f / 255.f, 165.f / 255.f, 1.f); // Win7 default desktop color
			//	//glClear(GL_COLOR_BUFFER_BIT);
			//	//glfwSwapBuffers(g_pHMDWindow);
			//	//glClear(GL_COLOR_BUFFER_BIT);
			//	//glfwSwapBuffers(g_pHMDWindow);

			//	//deallocateFBO(m_renderBuffer);
			//	ovrHmd_Destroy(hmd);
			//	ovr_Shutdown();

			//	glfwDestroyWindow(l_Window);
			//	glfwTerminate();
			//	exit(0);
			//}
			//else
			//{
			//	//destroyAuxiliaryWindow(l_Window);
			//	//glfwMakeContextCurrent(g_pHMDWindow);
			//}

			// Clean up FBO...


			//ovrHmd_Destroy(hmd);
			//ovr_Shutdown();

			//glfwDestroyWindow(l_Window);
			//glfwTerminate();
			//exit(EXIT_SUCCESS);
			break;
		case GLFW_KEY_R:
			ovrHmd_RecenterPose(hmd);
			break;
		case GLFW_KEY_UP:
			g_CameraPosition.z += 0.1f;
			break;
		case GLFW_KEY_DOWN:
			g_CameraPosition.z -= 0.1f;
			break;
		case GLFW_KEY_LEFT:
			g_CameraPosition.x += 0.1f;
			break;
		case GLFW_KEY_RIGHT:
			g_CameraPosition.x -= 0.1f;
			break;
		}

	}
}


int main(int argc, const char * argv[]) {
	printf("hello world\n");

	// Setup GLFW Window:
	l_Window = NULL;
	glfwSetErrorCallback(ErrorCallback);
	if (!glfwInit())
	{
		exit(EXIT_FAILURE);
	}

//#define USE_CORE_CONTEXT
#ifdef USE_CORE_CONTEXT	
#if defined(_MACOS)
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#else
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
#endif
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // Use the new OpenGL
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // Make OS X Happy. Shouldn't need this... I guess?
#endif

	glfwWindowHint(GLFW_SAMPLES, 4); // Use 4x antialiasing

	// Init HMD:
	ovr_Initialize();
	hmd = ovrHmd_Create(0);
	if (hmd) { // Get more details about the HMD.

		// Set size and position of the window:
		//const ovrSizei sz = g_app.getHmdResolution();
		//const ovrVector2i pos = g_app.getHmdWindowPos();
		resolution = hmd->Resolution;
		position = hmd->WindowsPos;
		printf("Resolution: %i, %i\n", resolution.h, resolution.w);

		// Check to see if we're using direct HMD mode or not:
		///@todo Why does ovrHmd_GetEnabledCaps always return 0 when querying the caps
		/// through the field in ovrHmd appears to work correctly?
		//const unsigned int caps = ovrHmd_GetEnabledCaps(m_Hmd);
		const unsigned int caps = hmd->HmdCaps;
		if ((caps & ovrHmdCap_ExtendDesktop) != 0)
		{
			directHmdMode = false;
		}
	} // Do something with the HMD. ....
	else {
		printf("No Oculus Rift device attached, using virtual version...\n");
		hmd = ovrHmd_CreateDebug(ovrHmd_DK2);
	}

	if (directHmdMode)
	{
		printf("Using Direct to Rift mode...\n");
		//LOG_INFO("Using Direct to Rift mode...\n");
		const GLFWmonitor* pPrimary = glfwGetPrimaryMonitor();
		int monitorCount = 0;
		GLFWmonitor** ppMonitors = glfwGetMonitors(&monitorCount);
		for (int i = 0; i<monitorCount; ++i)
		{
			l_Monitor = ppMonitors[i];
			if (l_Monitor == pPrimary)
				continue;
			//const GLFWvidmode* mode = glfwGetVideoMode(l_Monitor);
		}

		l_ClientSize.w = hmd->Resolution.w; // Something reasonable, smaller, but maintain aspect ratio...
		l_ClientSize.h = hmd->Resolution.h; // Something reasonable, smaller, but maintain aspect ratio...
	}
	else
	{
		int l_Count;
		GLFWmonitor** l_Monitors = glfwGetMonitors(&l_Count);
		switch (l_Count)
		{
		case 0:
			printf("No monitors found, exiting...\n");
			exit(EXIT_FAILURE);
			break;
		case 1:
			printf("Two monitors expected, found only one, using primary...\n");
			l_Monitor = glfwGetPrimaryMonitor();
			break;
		case 2:
			printf("Two monitors found, using second monitor...\n");
			l_Monitor = l_Monitors[1];
			break;
		default:
			printf("More than two monitors found, using second monitor...\n");
			l_Monitor = l_Monitors[1];
		}

		printf("Using Extended Desktop mode...\n");
		l_ClientSize.w = hmd->Resolution.w;
		l_ClientSize.h = hmd->Resolution.h;
	}

#if defined(__APPLE__)
	// Full screen is needed for OS X...
	l_Window = glfwCreateWindow(l_ClientSize.w, l_ClientSize.h, "GLFW Oculus Rift Test", l_Monitor, NULL);
#else
	// ... but it doesn't play well with direct mode in Windows 
	l_Window = glfwCreateWindow(l_ClientSize.w, l_ClientSize.h, "GLFW Oculus Rift Test", NULL, NULL);
#endif
	glfwWindowHint(GLFW_DECORATED, 0);


	// Attach the window in "Direct Mode"...
#if defined(_WIN32)
	if (directHmdMode)
	{
		ovrBool l_AttachResult = ovrHmd_AttachToWindow(hmd, glfwGetWin32Window(l_Window), NULL, NULL);
		if (!l_AttachResult)
		{
			printf("Could not attach to window...");
			exit(EXIT_FAILURE);
		}
	}
#endif

	glfwSetWindowPos(l_Window, position.x, position.y);
	//g_renderMode.outputType = RenderingMode::OVR_SDK; // Left over from riftskeleton...

	if (!l_Window)
	{
		printf("Failure!\n");
		glfwTerminate();
		exit(EXIT_FAILURE);
	}

	glfwMakeContextCurrent(l_Window);


#if !defined(__APPLE__)
	// Don't forget to initialize Glew, turn glewExperimental on to
	// avoid problems fetching function pointers...
	glewExperimental = GL_TRUE;
	const GLenum l_Result = glewInit();
	if (l_Result != GLEW_OK)
	{
		printf("glewInit() error.\n");
		glfwTerminate();
		exit(EXIT_FAILURE);
	}
#endif

	printGLContextInfo(l_Window);

	//=================
	// Finish setting up the framebuffers and the OpenGL scene:
	// (Taken from the single page opengl demo:	

	// Create some lights, materials, etc...
	SetOpenGLState();

	// Calculate the dimenstions of the Render Texture based on the field of view of each eye:
	// Find out what the texture sizes should be for each eye separately first...
	ovrSizei l_EyeTextureSizes[2];
	l_EyeTextureSizes[ovrEye_Left] = ovrHmd_GetFovTextureSize(hmd, ovrEye_Left, hmd->MaxEyeFov[ovrEye_Left], 1.0f);
	l_EyeTextureSizes[ovrEye_Right] = ovrHmd_GetFovTextureSize(hmd, ovrEye_Right, hmd->MaxEyeFov[ovrEye_Right], 1.0f);
	// Combine the dimensions for both eyes so we can use a single texture for the full display...
	g_RenderTargetSize.w = l_EyeTextureSizes[ovrEye_Left].w + l_EyeTextureSizes[ovrEye_Right].w;
	g_RenderTargetSize.h = (l_EyeTextureSizes[ovrEye_Left].h>l_EyeTextureSizes[ovrEye_Right].h ? l_EyeTextureSizes[ovrEye_Left].h : l_EyeTextureSizes[ovrEye_Right].h);


	//===========================
	// Configure our custom framebuffer that LibOVR will use to display our content:
	//
	// Create the FBO being a single one for both eyes (using one texture for both is open for debate)...
	// Because we are binding this buffer to the GL_FRAMEBUFFER OpenGL target, it will be used for 
	// both drawing and reading functions. This replaces the default OpenGL framebuffer and allows
	// us (and LibOVR) to control the rendering pipeline specifically instead of just writing to
	// the screen itself. LibOVR needs to read this framebuffer to be able to warp it during rendering.
	GLuint l_FBOId;
	glGenFramebuffers(1, &l_FBOId);
	glBindFramebuffer(GL_FRAMEBUFFER, l_FBOId);

	// Create the texture we're going to render to...
	GLuint l_TextureId;
	glGenTextures(1, &l_TextureId);
	// "Bind" the newly created texture : all future texture functions will modify this texture...
	glBindTexture(GL_TEXTURE_2D, l_TextureId);
	// Give an empty image to OpenGL (the last "0")
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_RenderTargetSize.w, g_RenderTargetSize.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	// Linear filtering...
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	// Create Depth Buffer (Depth Buffers are just special types of RenderBuffers)...
	GLuint l_DepthBufferId;
	glGenRenderbuffers(1, &l_DepthBufferId);
	glBindRenderbuffer(GL_RENDERBUFFER, l_DepthBufferId);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, g_RenderTargetSize.w, g_RenderTargetSize.h);
	// Bind the depth buffer (Z buffer) to our custom framebuffer (l_FBOId):
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, l_DepthBufferId);
	// Set the texture as our colour attachment #0...
	glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, l_TextureId, 0);

	// Set the list of draw buffers...
	GLenum l_GLDrawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
	glDrawBuffers(1, l_GLDrawBuffers); // "1" is the size of DrawBuffers

	// Check if everything is OK...
	GLenum l_Check = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
	if (l_Check != GL_FRAMEBUFFER_COMPLETE)
	{
		printf("There is a problem with the FBO.\n");
		exit(EXIT_FAILURE);
	}

	// Initialize the shader program we will use to render the scene:
	InitializeProgram();
	// Initialize the vertex buffer for use with the shader:
	InitializeVertexBuffer();


	// Initialize the vertex attribute array
	// All of the settings we configure for the shader program will be stored in the Vertex attribute array object
	// so that we won't have to bind all of this crap for every new object we want to render:
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	// Get the memory location of the offset uniform. Do NOT set "default uniform values" here. It breaks things.
	// I think this could be moved into InitializeProgram()... but I'm not sure how to organize all of this quite 
	// yet due to the use of the VAO. It could probably be moved anywhere after the program is linked, and before
	// any rendering is done.
	elapsedTimeUniform = glGetUniformLocation(theProgram, "time");
	GLuint loopDurationUnf = glGetUniformLocation(theProgram, "loopDuration");
	glUseProgram(theProgram);
	glUniform1f(loopDurationUnf, 5.0f);
	glUseProgram(0);

	// Bind our objects vertex buffer to opengl:
	// (I'm not sure why we have to bind this if we're using VAO's but maybe I'll see why later...?)
	glBindBuffer(GL_ARRAY_BUFFER, positionBufferObject);
	// Tell OpenGL to use the first two shader attibutes:
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	// Tell OpenGL where in the vertex buffer to get the data for each attribute:
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, 0);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, (void*)48);


	// Unbind all of that and get on with it...
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	// End OpenGL Framebuffer configuration
	//=====================================


	// Set configurations for each eye:
	// This includes the eye's dedicated texture size and the eyes location in the main texture buffer.
	// These values will be passed to LibOVR so that the library knows how to render each eye:
	// Left eye...
	g_EyeTextures[ovrEye_Left].Header.API = ovrRenderAPI_OpenGL;
	g_EyeTextures[ovrEye_Left].Header.TextureSize = g_RenderTargetSize;
	g_EyeTextures[ovrEye_Left].Header.RenderViewport.Pos.x = 0;
	g_EyeTextures[ovrEye_Left].Header.RenderViewport.Pos.y = 0;
	g_EyeTextures[ovrEye_Left].Header.RenderViewport.Size = l_EyeTextureSizes[ovrEye_Left];
	((ovrGLTexture&)(g_EyeTextures[ovrEye_Left])).OGL.TexId = l_TextureId;
	// Right eye (mostly the same as left but with the viewport on the right side of the texture)...
	g_EyeTextures[ovrEye_Right] = g_EyeTextures[ovrEye_Left];
	g_EyeTextures[ovrEye_Right].Header.RenderViewport.Pos.x = (g_RenderTargetSize.w + 1) / 2;
	g_EyeTextures[ovrEye_Right].Header.RenderViewport.Pos.y = 0;

	// Oculus Rift main texture buffer size configurations...
	g_Cfg.OGL.Header.API = ovrRenderAPI_OpenGL;
	//g_Cfg.OGL.Header.BackBufferSize.w = l_ClientSize.w;
	//g_Cfg.OGL.Header.BackBufferSize.h = l_ClientSize.h;
	g_Cfg.OGL.Header.BackBufferSize = Sizei(l_ClientSize.w, l_ClientSize.h);
	g_Cfg.OGL.Header.Multisample = (l_MultiSampling ? 1 : 0);
#if defined(_WIN32)
	g_Cfg.OGL.Window = glfwGetWin32Window(l_Window);
	g_Cfg.OGL.DC = GetDC(g_Cfg.OGL.Window);
#elif defined(__linux__)
	//l_Cfg.OGL.Win = glfwGetX11Window(l_Window);
	//l_Cfg.OGL.Disp = glfwGetX11Display();
#endif

	// Enable capabilities...
	ovrHmd_SetEnabledCaps(hmd, ovrHmdCap_LowPersistence | ovrHmdCap_DynamicPrediction);

	// Actually configure Oculus LibOVR rendering using the configurations we set up above (and stored in g_Cfg.OGL):
	ovrBool l_ConfigureResult = ovrHmd_ConfigureRendering(hmd, &g_Cfg.Config, g_DistortionCaps, hmd->MaxEyeFov, g_EyeRenderDesc);
	glUseProgram(0); // Avoid OpenGL state leak in ovrHmd_ConfigureRendering...
	if (!l_ConfigureResult)
	{
		printf("Configure failed.\n");
		exit(EXIT_FAILURE);
	}


	// Projection matrici for each eye will not change at runtime, we can set them here...
	g_ProjectionMatrici[ovrEye_Left] = ovrMatrix4f_Projection(g_EyeRenderDesc[ovrEye_Left].Fov, 0.3f, 100.0f, true);
	g_ProjectionMatrici[ovrEye_Right] = ovrMatrix4f_Projection(g_EyeRenderDesc[ovrEye_Right].Fov, 0.3f, 100.0f, true);

	// IPD offset values will not change at runtime, we can set them here...
	g_EyeOffsets[ovrEye_Left] = g_EyeRenderDesc[ovrEye_Left].HmdToEyeViewOffset;
	g_EyeOffsets[ovrEye_Right] = g_EyeRenderDesc[ovrEye_Right].HmdToEyeViewOffset;

	// Initial camera position...
	g_CameraPosition.x = 0.0f;
	g_CameraPosition.y = 0.0f;
	g_CameraPosition.z = -2.0f;


	// Start the sensor which provides the Rift's pose and motion.
	ovrBool l_TrackingResult = ovrHmd_ConfigureTracking(hmd, ovrTrackingCap_Orientation |
		ovrTrackingCap_MagYawCorrection |
		ovrTrackingCap_Position, 0);
	if (!l_TrackingResult)
	{
		printf("Could not start tracking...");
		exit(EXIT_FAILURE);
	}

	// Setup GLFW Callbacks:
	glfwSetWindowSizeCallback(l_Window, WindowSizeCallback);
	//glfwSetMouseButtonCallback(l_Window, mouseDown);
	//glfwSetCursorPosCallback(l_Window, mouseMove);
	//glfwSetScrollCallback(l_Window, mouseWheel);
	glfwSetKeyCallback(l_Window, keyboard);


	// Do a single recenter to calibrate orientation to current state of the Rift...
	ovrHmd_RecenterPose(hmd);


	//=====================
	// Scene variables:
	GLfloat l_SpinX;
	GLfloat l_SpinY;
	const bool l_Spin = false;

	// Head tracking:
	float yaw;
	float eyePitch;
	float eyeRoll;

	// Begin draw loop:
	unsigned int l_FrameIndex = 0;
	while (!glfwWindowShouldClose(l_Window)) {

		// Begin the frame...
		ovrHmd_BeginFrame(hmd, l_FrameIndex);

		// Get eye poses for both the left and the right eye. g_EyePoses contains all Rift information: orientation, positional tracking and
		// the IPD in the form of the input variable g_EyeOffsets.
		ovrHmd_GetEyePoses(hmd, l_FrameIndex, g_EyeOffsets, g_EyePoses, NULL);

		// Bind our custom FBO (instead of using the default OpenGL framebuffer)...
		glBindFramebuffer(GL_FRAMEBUFFER, l_FBOId);

		// Clear...
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		for (int l_EyeIndex = 0; l_EyeIndex<ovrEye_Count; l_EyeIndex++)
		{
			ovrEyeType l_Eye = hmd->EyeRenderOrder[l_EyeIndex];

			glViewport(
				g_EyeTextures[l_Eye].Header.RenderViewport.Pos.x,
				g_EyeTextures[l_Eye].Header.RenderViewport.Pos.y,
				g_EyeTextures[l_Eye].Header.RenderViewport.Size.w,
				g_EyeTextures[l_Eye].Header.RenderViewport.Size.h
				);

			// Pass projection matrix on to OpenGL...
			glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			glMultMatrixf(&(g_ProjectionMatrici[l_Eye].Transposed().M[0][0]));

			// Create the model-view matrix and pass on to OpenGL...
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();

			// Multiply with orientation retrieved from sensor...
			OVR::Quatf l_Orientation = OVR::Quatf(g_EyePoses[l_Eye].Orientation);
			OVR::Matrix4f l_ModelViewMatrix = OVR::Matrix4f(l_Orientation.Inverted());
			glMultMatrixf(&(l_ModelViewMatrix.Transposed().M[0][0]));

			// Translation due to positional tracking (DK2) and IPD...
			glTranslatef(-g_EyePoses[l_Eye].Position.x, -g_EyePoses[l_Eye].Position.y, -g_EyePoses[l_Eye].Position.z);

			// Move the world forward a bit to show the scene in front of us...
			glTranslatef(g_CameraPosition.x, g_CameraPosition.y, g_CameraPosition.z);

			// (Re)set the light positions so they don't move along with the cube...
			SetStaticLightPositions();

			// Make the cube spin...
			if (l_Spin)
			{
				l_SpinX = (GLfloat)fmod(glfwGetTime()*17.0, 360.0);
				l_SpinY = (GLfloat)fmod(glfwGetTime()*23.0, 360.0);
			}
			else
			{
				l_SpinX = 30.0f;
				l_SpinY = 40.0f;
			}
			glRotatef(l_SpinX, 1.0f, 0.0f, 0.0f);
			glRotatef(l_SpinY, 0.0f, 1.0f, 0.0f);

			// Use old render functions to draw a cube...
			// RenderCubeFixedFunction();
			//RenderCubeVertexArrays();

			// Use shader program to render instead:

			glUseProgram(theProgram);
			glBindVertexArray(vao);

			glUniform1f(elapsedTimeUniform, (float)glfwGetTime());

			//glBindBuffer(GL_ARRAY_BUFFER, positionBufferObject);
			//glEnableVertexAttribArray(0);
			//glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, 0);

			glDrawArrays(GL_TRIANGLES, 0, 3);

			//glDisableVertexAttribArray(0);
			glBindVertexArray(0);
			glUseProgram(0);

			
		}

		// Query the HMD for the current tracking state.
		ovrTrackingState ts = ovrHmd_GetTrackingState(hmd, ovr_GetTimeInSeconds());
		if (ts.StatusFlags & (ovrStatus_OrientationTracked | ovrStatus_PositionTracked))
		{
			ovrPosef pose = ts.HeadPose.ThePose;
			//printf("Pos: %f, %f, %f\t", pose.Position.x, pose.Position.y, pose.Position.z);

			ovrQuatf ori = pose.Orientation;
			Quatf OculusRiftOrientation = ori;
			OculusRiftOrientation.GetEulerAngles<OVR::Axis_X, OVR::Axis_Y, OVR::Axis_Z>(&yaw, &eyePitch, &eyeRoll);
			//printf("Ori: %f, %f, %f\n", yaw, eyePitch, eyeRoll);
		}
		else {
			//printf("No tracking yet...\n");
		}

		// Back to the default framebuffer...
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		// Do everything, distortion, front/back buffer swap...
		ovrHmd_EndFrame(hmd, g_EyePoses, g_EyeTextures);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); // Avoid OpenGL state leak in ovrHmd_EndFrame...
		glBindBuffer(GL_ARRAY_BUFFER, 0); // Avoid OpenGL state leak in ovrHmd_EndFrame...

		++l_FrameIndex;

		glfwPollEvents();

	}// End head tracking.


	glDeleteRenderbuffers(1, &l_DepthBufferId);
	glDeleteTextures(1, &l_TextureId);
	glDeleteFramebuffers(1, &l_FBOId);

	ovrHmd_Destroy(hmd);
	ovr_Shutdown();

	// Clean up window...
	glfwDestroyWindow(l_Window);
	glfwTerminate();

	exit(EXIT_SUCCESS);

}