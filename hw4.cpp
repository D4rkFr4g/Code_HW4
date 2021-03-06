////////////////////////////////////////////////////////////////////////
//
//   Harvard University
//   CS175 : Computer Graphics
//   Professor Steven Gortler
//
////////////////////////////////////////////////////////////////////////
/****************************************************** 
* Project:         CS 116A Homework #4
* File:            hw4.cpp 
* Purpose:         Implement a curve drawwing algorithm
* Start date:      11/10/13 
* Programmer:      Zane Melcho 
* 
****************************************************** 
*/

#include <vector>
#include <string>
#include <memory>
#include <stdexcept>
#include <stack>
#if __GNUG__
#   include <tr1/memory>
#endif

#include <GL/glew.h>
#ifdef __MAC__
#   include <GLUT/glut.h>
#else
#   include <GL/glut.h>
#endif


#include "cvec.h"
#include "matrix4.h"
#include "geometrymaker.h"
#include "ppm.h"
#include "glsupport.h"
#include "rigtform.h"
#include "splineReader.h"
#include "catmullRomSpline.h"

#define M_PI 3.1415926535897932384626433832795;
enum {I_POWER, I_SLERP, I_LERP, PAUSE};


using namespace std;      // for string, vector, iostream, and other standard C++ stuff
using namespace tr1; // for shared_ptr

// G L O B A L S ///////////////////////////////////////////////////

// --------- IMPORTANT --------------------------------------------------------
// Before you start working on this assignment, set the following variable
// properly to indicate whether you want to use OpenGL 2.x with GLSL 1.0 or
// OpenGL 3.x+ with GLSL 1.3.
//
// Set g_Gl2Compatible = true to use GLSL 1.0 and g_Gl2Compatible = false to
// use GLSL 1.3. Make sure that your machine supports the version of GLSL you
// are using. In particular, on Mac OS X currently there is no way of using
// OpenGL 3.x with GLSL 1.3 when GLUT is used.
//
// If g_Gl2Compatible=true, shaders with -gl2 suffix will be loaded.
// If g_Gl2Compatible=false, shaders with -gl3 suffix will be loaded.
// To complete the assignment you only need to edit the shader files that get
// loaded
// ----------------------------------------------------------------------------
static const bool g_Gl2Compatible = false;

// Forward Declarations
struct RigidBody;
static RigidBody* makeDomino();

static const float g_frustMinFov = 60.0;  // A minimal of 60 degree field of view
static float g_frustFovY = g_frustMinFov; // FOV in y direction (updated by updateFrustFovY)

static const float g_frustNear = -0.1;    // near plane
static const float g_frustFar = -50.0;    // far plane
static const float g_groundY = 0.0;      // y coordinate of the ground
static const float g_groundSize = 27.0;   // half the ground length

static int g_windowWidth = 512;
static int g_windowHeight = 512;
static bool g_mouseClickDown = false;    // is the mouse button pressed
static bool g_mouseLClickButton, g_mouseRClickButton, g_mouseMClickButton;
static int g_mouseClickX, g_mouseClickY; // coordinates for mouse click event
static int g_activeShader = 0;
static int g_numOfObjects = 0; //Number of objects to be drawn
static int g_numOfControlPoints = 0;
static float g_framesPerSecond = 32;
static int g_interpolationType  = I_POWER;
static bool isKeyboardActive = true;
static Cvec3* g_splineArray;
static int g_numOfInterpolantDominos = 5;

struct ShaderState {
  GlProgram program;

  // Handles to uniform variables
  GLint h_uLight, h_uLight2;
  GLint h_uProjMatrix;
  GLint h_uModelViewMatrix;
  GLint h_uNormalMatrix;
  GLint h_uColor;

  // Handles to vertex attributes
  GLint h_aPosition;
  GLint h_aNormal;

  ShaderState(const char* vsfn, const char* fsfn) {
    readAndCompileShader(program, vsfn, fsfn);

    const GLuint h = program; // short hand

    // Retrieve handles to uniform variables
    h_uLight = safe_glGetUniformLocation(h, "uLight");
    h_uLight2 = safe_glGetUniformLocation(h, "uLight2");
    h_uProjMatrix = safe_glGetUniformLocation(h, "uProjMatrix");
    h_uModelViewMatrix = safe_glGetUniformLocation(h, "uModelViewMatrix");
    h_uNormalMatrix = safe_glGetUniformLocation(h, "uNormalMatrix");
    h_uColor = safe_glGetUniformLocation(h, "uColor");

    // Retrieve handles to vertex attributes
    h_aPosition = safe_glGetAttribLocation(h, "aPosition");
    h_aNormal = safe_glGetAttribLocation(h, "aNormal");

    if (!g_Gl2Compatible)
      glBindFragDataLocation(h, 0, "fragColor");
    checkGlErrors();
  }

};

static const int g_numShaders = 2;
static const char * const g_shaderFiles[g_numShaders][2] = {
  {"./shaders/basic-gl3.vshader", "./shaders/diffuse-gl3.fshader"},
  {"./shaders/basic-gl3.vshader", "./shaders/solid-gl3.fshader"}
};
static const char * const g_shaderFilesGl2[g_numShaders][2] = {
  {"./shaders/basic-gl2.vshader", "./shaders/diffuse-gl2.fshader"},
  {"./shaders/basic-gl2.vshader", "./shaders/solid-gl2.fshader"}
};
static vector<shared_ptr<ShaderState> > g_shaderStates; // our global shader states

// --------- Geometry

// Macro used to obtain relative offset of a field within a struct
#define FIELD_OFFSET(StructType, field) &(((StructType *)0)->field)

// A vertex with floating point position and normal
struct VertexPN {
  Cvec3f p, n;

  VertexPN() {}
  VertexPN(float x, float y, float z,
           float nx, float ny, float nz)
    : p(x,y,z), n(nx, ny, nz)
  {}

  // Define copy constructor and assignment operator from GenericVertex so we can
  // use make* functions from geometrymaker.h
  VertexPN(const GenericVertex& v) {
    *this = v;
  }

  VertexPN& operator = (const GenericVertex& v) {
    p = v.pos;
    n = v.normal;
    return *this;
  }
};

struct Geometry {
  GlBufferObject vbo, ibo;
  int vboLen, iboLen;

  Geometry(VertexPN *vtx, unsigned short *idx, int vboLen, int iboLen) {
    this->vboLen = vboLen;
    this->iboLen = iboLen;

    // Now create the VBO and IBO
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(VertexPN) * vboLen, vtx, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned short) * iboLen, idx, GL_STATIC_DRAW);
  }

  void draw(const ShaderState& curSS) {
    // Enable the attributes used by our shader
    safe_glEnableVertexAttribArray(curSS.h_aPosition);
    safe_glEnableVertexAttribArray(curSS.h_aNormal);

    // bind vbo
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    safe_glVertexAttribPointer(curSS.h_aPosition, 3, GL_FLOAT, GL_FALSE, sizeof(VertexPN), FIELD_OFFSET(VertexPN, p));
    safe_glVertexAttribPointer(curSS.h_aNormal, 3, GL_FLOAT, GL_FALSE, sizeof(VertexPN), FIELD_OFFSET(VertexPN, n));

    // bind ibo
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

    // draw!
    glDrawElements(GL_TRIANGLES, iboLen, GL_UNSIGNED_SHORT, 0);

    // Disable the attributes used by our shader
    safe_glDisableVertexAttribArray(curSS.h_aPosition);
    safe_glDisableVertexAttribArray(curSS.h_aNormal);
  }

	void draw(const ShaderState& curSS, Matrix4 MVM)
	{
		Matrix4 NMVM = normalMatrix(MVM);

		GLfloat glmatrix[16];
		MVM.writeToColumnMajorMatrix(glmatrix); // send MVM
		safe_glUniformMatrix4fv(curSS.h_uModelViewMatrix, glmatrix);

		NMVM.writeToColumnMajorMatrix(glmatrix); // send NMVM
		safe_glUniformMatrix4fv(curSS.h_uNormalMatrix, glmatrix);

		draw(curSS);
	}
};
/*-----------------------------------------------*/
struct RigidBody
{
	RigTForm rtf;
	Matrix4 scale;
	RigidBody **children;
	int numOfChildren;
	Cvec3 color;
	Geometry *geom;
	bool isVisible;
	bool isChildVisible;
	string name;

	RigidBody()
	{
		rtf = RigTForm();
		scale = Matrix4();
		children = NULL;
		numOfChildren = 0;
		color = Cvec3(.5,.5,.5);
		geom = NULL;
		isVisible = true;
		isChildVisible = true;
	}

	~RigidBody()
	{
		for (int i =0; i < numOfChildren; i++)
			delete children[i];
		delete []children;
		delete geom;
	}

	RigidBody(RigTForm rtf_, Matrix4 scale_, RigidBody **children_, Geometry *geom_, Cvec3 color_)
	{
		/* PURPOSE:		 
			RECEIVES:	 
							
			RETURNS:		
		*/

		rtf = rtf_;
		scale = scale_;
		children = children_;
		numOfChildren = 0;
		geom = geom_;
		color = color_;
		isVisible = true;
	}

	void drawRigidBody(const ShaderState& curSS, RigTForm invEyeRbt)
	{
		RigTForm respectFrame = invEyeRbt;
		draw(curSS, respectFrame, Matrix4());
	}

	void draw(const ShaderState& curSS, RigTForm respectFrame_, Matrix4 respectScale_)
	{
		safe_glUniform3f(curSS.h_uColor, color[0], color[1], color[2]);
			
		//Draw parent
		this;
	
		//scale correct but not translated correctly; moving one object scale moves the rest;
		RigTForm respectFrame = respectFrame_ * rtf;
		Matrix4 respectScale = respectScale_ * scale;

		//Matrix4 MVM = RigTForm::makeTRmatrix(respectFrame) * scale;

		Matrix4 MVM = RigTForm::makeTRmatrix(respectFrame, respectScale);
		
		/*/
		//Positioning doesn't change after scales; Moving one object doesn't translate children during setup
		RigTForm respectFrame = respectFrame_ * rtf;
		Matrix4 respectScale = respectScale_ * scale;
		Matrix4 temp1 = RigTForm::makeTRmatrix(respectFrame_) * respectScale_;
		Matrix4 temp2 = RigTForm::makeTRmatrix(rtf) * scale;
		Matrix4 MVM = temp1 * temp2;
		*/

		if (isVisible)
		{
			if (geom != NULL)
				geom->draw(curSS, MVM);
		}

		//Draw Children
		if (isChildVisible)
		{
			for (int i = 0; i < numOfChildren; i++)
			{
				children[i]->draw(curSS, respectFrame, respectScale);
			}
		}
		
	}

	void draw(const ShaderState& curSS, Matrix4 respectFrame_)
	{
		safe_glUniform3f(curSS.h_uColor, color[0], color[1], color[2]);
			
		//Draw parent
		Matrix4 respectFrame = respectFrame_ * RigTForm::makeTRmatrix(rtf, scale);
		//Matrix4 respectFrame = RigTForm::makeTRmatrix(rtf, scale) * respectFrame_;
		Matrix4 MVM = respectFrame;

		if (isVisible)
		{
			if (geom != NULL)
				geom->draw(curSS, MVM);
		}

		//Draw Children
		for (int i = 0; i < numOfChildren; i++)
		{
			children[i]->draw(curSS, respectFrame);
		}
	}
};
/*-----------------------------------------------*/
// Vertex buffer and index buffer associated with the ground and cube geometry
static shared_ptr<Geometry> g_ground, g_cube, g_sphere;

// --------- Scene

static const Cvec3 g_light1(2.0, 3.0, 14.0), g_light2(-2, -3.0, -5.0);  // define two lights positions in world space
static RigTForm g_skyRbt = RigTForm(Cvec3(0.0, 3, 10.0)); // Default camera
static RigTForm g_eyeRbt = g_skyRbt; //Set the g_eyeRbt frame to be default as the sky frame
static RigidBody* g_rigidBodies;//[g_numOfObjects]; // Array that holds each Rigid Body Object

///////////////// END OF G L O B A L S //////////////////////////////////////////////////
/*-----------------------------------------------*/
static Matrix4 lookAt(Cvec3f eyePosition, Cvec3f lookAtPosition, Cvec3f upVector)
{
	Cvec3f x, y, z, w;
	double m[16];

	//Different from the book but works correctly
	z = normalize(eyePosition - lookAtPosition);
	x = normalize(cross(upVector,z));
	y = cross(z,x);	

	int k = 0;

	for (int i = 0; i < 3; i++)
	{
		m[k] = x[i];
		k++;
		m[k] = y[i];
		k++;
		m[k] = z[i];
		k++;
		m[k] = eyePosition[i];
		k++;
	}

	m[12] = 0;
	m[13] = 0;
	m[14] = 0;
	m[15] = 1;

	//return Matrix4();

	return Matrix4(m, true);
}
/*-----------------------------------------------*/
static float angleBetween(Cvec3 vectorOne, Cvec3 vectorTwo)
{
	float temp = dot(vectorOne, vectorTwo);
	float vOneNorm = norm(vectorOne);
	float vTwoNorm = norm(vectorTwo);
	temp = (temp / (vOneNorm * vTwoNorm));
	temp = acos(temp) * 180;
	temp /= M_PI;

	//cout << "angle = " << temp << "\n";
	//Matrix4::print(Matrix4::makeXRotation(temp));

	return temp;
}
/*-----------------------------------------------*/
static float lookAt(Cvec3 eyePosition, Cvec3 upPosition)
{
	/*
	float temp = dot(eyePosition, upPosition);
	float eyeNorm = norm(eyePosition);
	float atNorm = norm(upPosition);
	temp = (temp / (eyeNorm * atNorm));
	temp = acos(temp) * 180;
	temp /= M_PI;
	*/

	return -(90 - angleBetween(eyePosition, upPosition));
}
/*-----------------------------------------------*/
static void lookAtOrigin()
{
	// Set angle to look at the origin
	Cvec3 eye = g_eyeRbt.getTranslation();
	Cvec3 up = Cvec3(0,1,0);
	g_eyeRbt.setRotation(Quat().makeXRotation(lookAt(eye,up)));
}
/*-----------------------------------------------*/
static void initCamera()
{
	Cvec3 eye = Cvec3(0.0, 3.0, 10.0);
	Cvec3 at = Cvec3(0.0, 0.0, 0.0);
	Cvec3 up = Cvec3(0.0,1.0,0.0);
	//g_skyRbt = lookAt(eye, at, up); // Default camera
	g_skyRbt.setRotation(Quat().makeXRotation(lookAt(eye,up))); // TODO Change so lookat is done after conversion to Matrix
	g_eyeRbt = g_skyRbt;
}
/*-----------------------------------------------*/
static void initGround() {
  // A x-z plane at y = g_groundY of dimension [-g_groundSize, g_groundSize]^2
  VertexPN vtx[4] = {
    VertexPN(-g_groundSize, g_groundY, -g_groundSize, 0, 1, 0),
    VertexPN(-g_groundSize, g_groundY,  g_groundSize, 0, 1, 0),
    VertexPN( g_groundSize, g_groundY,  g_groundSize, 0, 1, 0),
    VertexPN( g_groundSize, g_groundY, -g_groundSize, 0, 1, 0),
  };
  unsigned short idx[] = {0, 1, 2, 0, 2, 3};
  g_ground.reset(new Geometry(&vtx[0], &idx[0], 4, 6));
}
/*-----------------------------------------------*/
static Geometry* initCubes() 
{
  int ibLen, vbLen;
  getCubeVbIbLen(vbLen, ibLen);

  // Temporary storage for cube geometry
  vector<VertexPN> vtx(vbLen);
  vector<unsigned short> idx(ibLen);

  makeCube(1, vtx.begin(), idx.begin());
  return new Geometry(&vtx[0], &idx[0], vbLen, ibLen);
}
/*-----------------------------------------------*/
static Geometry* initSpheres() 
{
	int slices = 20;
	int stacks = 20;
	float radius = 1;
	int ibLen, vbLen;
	getSphereVbIbLen(slices, stacks, vbLen, ibLen);

	// Temporary storage for cube geometry
	vector<VertexPN> vtx(vbLen);
	vector<unsigned short> idx(ibLen);

	makeSphere(radius, slices, stacks, vtx.begin(), idx.begin());
	return new Geometry(&vtx[0], &idx[0], vbLen, ibLen);
}

static void initDominos()
{
	/* PURPOSE:		Initializes each domino to it's position, scale, and rotation  
	*/
	int extraDominos = (g_numOfControlPoints - 4) * g_numOfInterpolantDominos;
	int numOfDominos = g_numOfControlPoints + extraDominos;
	g_numOfObjects = numOfDominos;	// Could cause an issue if a non domino object is added
	g_rigidBodies = new RigidBody[numOfDominos];

	// Build Dominos
	for (int i = 0; i < numOfDominos; i++)
	{
		RigidBody *domino;
		domino = makeDomino();
		g_rigidBodies[i] = *domino;
	}

	// Set Domino Positions
	for (int i = 0; i < numOfDominos; i++)
	{
		Cvec3 position;
		Quat rotation;

		// Set position according to the Spline file except the extra dominos which start at the first point
		if ( i < numOfDominos - extraDominos)
			position = g_splineArray[i];
		else
			position = g_splineArray[0];

		// Set color of control dominos
		if ( i < numOfDominos - extraDominos )
			g_rigidBodies[i].children[0]->color = Cvec3(0.05, 0.05, 0.05);

		g_rigidBodies[i].rtf.setTranslation(position);

		// Set Rotations
		if ( i > 0 && i < numOfDominos - (extraDominos + 1))
		{
			Cvec3 screen = Cvec3(0,0,1);
			Cvec3 vector = g_splineArray[i+1] - g_splineArray[i-1];
			float angle = angleBetween(vector, screen);

			// Rotates the short way
			if (vector[0] < 0 && vector[2] < 0)
				angle *= -1;

			rotation = Quat().makeYRotation(angle);
		}
		else
			rotation = g_rigidBodies[0].rtf.getRotation();
		
		g_rigidBodies[i].rtf.setRotation(rotation);
	}

	// Hide the extra Dominos
	for (int i = numOfDominos - 1; i >= numOfDominos - extraDominos; i--)
	{
		g_rigidBodies[i].isChildVisible = false;
	}

	glutPostRedisplay();
}
/*-----------------------------------------------*/
static RigidBody* makeDomino()
{
	/* PURPOSE:		Creates a domino object  
		RETURNS:    RigidBody* that points to the domino
		REMARKS:		Creates a snake-eyes domino only
	*/

	float height = 4.0;
	float width = 1.0;
	float thick = 0.5;

	RigTForm rigTemp = RigTForm();
	Matrix4 scaleTemp = Matrix4();
	
	// Make container
	RigidBody *domino = new RigidBody(RigTForm(), Matrix4(), NULL, initCubes(), Cvec3(0.5, 0.5, 0.5));
	domino->isVisible = false;
	domino->name = "container";

	// Make body
	rigTemp = RigTForm(Cvec3(0, 0, 0));
	scaleTemp = Matrix4::makeScale(Cvec3(width, height, thick));

	RigidBody *body = new RigidBody(rigTemp, scaleTemp, NULL, initCubes(), Cvec3(0,0,0));
	body->name = "body";

	// Make bar
	rigTemp = RigTForm(Cvec3(0, 0, thick * .5));
	scaleTemp = Matrix4::makeScale(Cvec3(0.75, 0.05, 0.005));

	RigidBody *bar = new RigidBody(rigTemp, scaleTemp, NULL, initCubes(), Cvec3(1,1,1));
	bar->name = "bar";

	// Make Dots
	rigTemp = RigTForm(Cvec3(0, height * 0.25, thick * .51));
	scaleTemp = Matrix4::makeScale(Cvec3(.1, .1, .005)) * inv(body->scale);

	RigidBody *dot0 = new RigidBody(rigTemp, scaleTemp, NULL, initSpheres(), Cvec3(1,1,1));
	dot0->name = "dot0";

	rigTemp = RigTForm(Cvec3(0, -height * 0.25, thick * .51));
	scaleTemp = Matrix4::makeScale(Cvec3(.1, .1, .005)) * inv(body->scale);

	RigidBody *dot1 = new RigidBody(rigTemp, scaleTemp, NULL, initSpheres(), Cvec3(1,1,1));
	dot1->name = "dot1";

	//Setup Children
	domino->numOfChildren = 1;
	body->numOfChildren = 3;

	domino->children = new RigidBody*[domino->numOfChildren];
	domino->children[0] = body;

	body->children = new RigidBody*[body->numOfChildren];
	body->children[0] = bar;
	body->children[1] = dot0;
	body->children[2] = dot1;

	return domino;
}
/*-----------------------------------------------*/
// takes a projection matrix and send to the the shaders
static void sendProjectionMatrix(const ShaderState& curSS, const Matrix4& projMatrix) {
  GLfloat glmatrix[16];
  projMatrix.writeToColumnMajorMatrix(glmatrix); // send projection matrix
  safe_glUniformMatrix4fv(curSS.h_uProjMatrix, glmatrix);
}

// takes MVM and its normal matrix to the shaders
static void sendModelViewNormalMatrix(const ShaderState& curSS, const Matrix4& MVM, const Matrix4& NMVM) {
  GLfloat glmatrix[16];
  MVM.writeToColumnMajorMatrix(glmatrix); // send MVM
  safe_glUniformMatrix4fv(curSS.h_uModelViewMatrix, glmatrix);

  NMVM.writeToColumnMajorMatrix(glmatrix); // send NMVM
  safe_glUniformMatrix4fv(curSS.h_uNormalMatrix, glmatrix);
}

// update g_frustFovY from g_frustMinFov, g_windowWidth, and g_windowHeight
static void updateFrustFovY() {
  if (g_windowWidth >= g_windowHeight)
    g_frustFovY = g_frustMinFov;
  else {
    const double RAD_PER_DEG = 0.5 * CS175_PI/180;
    g_frustFovY = atan2(sin(g_frustMinFov * RAD_PER_DEG) * g_windowHeight / g_windowWidth, cos(g_frustMinFov * RAD_PER_DEG)) / RAD_PER_DEG;
  }
}

static Matrix4 makeProjectionMatrix() {
  return Matrix4::makeProjection(
           g_frustFovY, g_windowWidth / static_cast <double> (g_windowHeight),
           g_frustNear, g_frustFar);
}
/*-----------------------------------------------*/
static void drawStuff() 
{
	/* PURPOSE:		Draws objects in relative 3d space  
	*/

	// short hand for current shader state
	const ShaderState& curSS = *g_shaderStates[g_activeShader];

	// build & send proj. matrix to vshader
	const Matrix4 projmat = makeProjectionMatrix();
	sendProjectionMatrix(curSS, projmat);

	// Use the g_eyeRbt as the eyeRbt;
	const RigTForm eyeRbt = g_eyeRbt;
	const RigTForm invEyeRbt = inv(eyeRbt);

	const Cvec3 eyeLight1 = Cvec3(invEyeRbt * Cvec4(g_light1, 1)); // g_light1 position in eye coordinates
	const Cvec3 eyeLight2 = Cvec3(invEyeRbt * Cvec4(g_light2, 1)); // g_light2 position in eye coordinates
	safe_glUniform3f(curSS.h_uLight, eyeLight1[0], eyeLight1[1], eyeLight1[2]);
	safe_glUniform3f(curSS.h_uLight2, eyeLight2[0], eyeLight2[1], eyeLight2[2]);

	// draw ground
	// ===========
	//
	const RigTForm groundRbt = RigTForm();  // identity
	Matrix4 MVM = RigTForm::makeTRmatrix(invEyeRbt * groundRbt, Matrix4());
	Matrix4 NMVM = normalMatrix(MVM);
	sendModelViewNormalMatrix(curSS, MVM, NMVM);
	safe_glUniform3f(curSS.h_uColor, 0.1, 0.95, 0.1); // set color
	g_ground->draw(curSS);

	// Draw all Rigid body objects
	for (int i = 0; i < g_numOfObjects; i++)
		g_rigidBodies[i].drawRigidBody(curSS, invEyeRbt);
}
/*-----------------------------------------------*/
static void display() {
  glUseProgram(g_shaderStates[g_activeShader]->program);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);                   // clear framebuffer color&depth

  drawStuff();

  glutSwapBuffers();                                    // show the back buffer (where we rendered stuff)

  checkGlErrors();
}

static void reshape(const int w, const int h) {
  g_windowWidth = w;
  g_windowHeight = h;
  glViewport(0, 0, w, h);
  cerr << "Size of window is now " << w << "x" << h << endl;
  updateFrustFovY();
  glutPostRedisplay();
}

static void motion(const int x, const int y) {
	const double dx = x - g_mouseClickX;
	const double dy = g_windowHeight - y - 1 - g_mouseClickY;

	RigTForm m;
	if (g_mouseLClickButton && !g_mouseRClickButton) { // left button down?
		m = g_eyeRbt * RigTForm(Quat().makeXRotation(dy)) * RigTForm(Quat().makeYRotation(-dx)) * inv(g_eyeRbt);
	}
  else if (g_mouseRClickButton && !g_mouseLClickButton) 
  { // right button down?
		m = g_eyeRbt * RigTForm(Cvec3(dx, dy, 0) * 0.01) * inv(g_eyeRbt); //Update based on Eye Frame
	//m = Matrix4::makeTranslation(Cvec3(dx, dy, 0) * 0.01);
  }
  else if (g_mouseMClickButton || (g_mouseLClickButton && g_mouseRClickButton)) {  // middle or (left and right) button down?
    //m = g_eyeRbt * Matrix4::makeTranslation(Cvec3(0, 0, -dy) * 0.01) * inv(g_eyeRbt); //Update based on Eye Frame
	  m = g_eyeRbt * RigTForm(Cvec3(0, 0,dy) * 0.01) * inv(g_eyeRbt); //Update based on Eye Frame
  }

  if (g_mouseClickDown) {
//	  g_objectRbt[0] *= m; // Simply right-multiply is WRONG
	  //g_rigidBodies[0].rtf = m * g_rigidBodies[0].rtf; //Update Robot Container
	  g_eyeRbt = m * g_eyeRbt; //Update Camera
	  glutPostRedisplay(); // we always redraw if we changed the scene
  }

  g_mouseClickX = x;
  g_mouseClickY = g_windowHeight - y - 1;
}

/*-----------------------------------------------*/
static void keyboardTimer(int value)
{
	// Flip Keyboard
	isKeyboardActive = !isKeyboardActive;

	float msecs = 10 * 1000;
	
	if (!isKeyboardActive)
		glutTimerFunc(msecs, keyboardTimer, 0);
}
/*-----------------------------------------------*/
static void animate(int value)
{
	// Static Animation General Variables
	static float stopwatch = 0;
	float msecsPerFrame = 1/(g_framesPerSecond / 1000);
	static int animationPart = 0;
	static bool isAnimating = true;
	const static float stepsPerSecond = 10.0/2.0; // Time Allowed / Steps taken
	static float totalTime = stepsPerSecond * 1 * 1000;
	static float elapsedTime = 0;
	static bool isFirstEntry = true;

	// Static Animation Specific Variables
	//static float 
	
	// Used to reset variables every time animation is run
	if (isFirstEntry)
	{
		// Animation General Resets
		isAnimating =  true;
		stopwatch = 0;
		animationPart = 0;

		// Animation Specific Resets

		// Must be here
		isFirstEntry = false;
	}

	//Handles which part of animation is currently running
	if (elapsedTime >= totalTime)
	{
		//g_eyeRbt.setRotation(end.getRotation());

		if (animationPart == 0)
		{

		}
		else
		{
			glutPostRedisplay();
			isAnimating = false;
		}
		//Reset values to default for next Animation Part		
		totalTime = stepsPerSecond * 1 * 1000;
		elapsedTime = 0;

		animationPart++;
	}

	if (isAnimating)
	{
		// Determine percentage of animation
		float alpha = elapsedTime / totalTime;


		// Update Times and redisplay
		elapsedTime += msecsPerFrame;
		glutPostRedisplay();
	
		//Time total animation for Debugging
		stopwatch += msecsPerFrame;

		//Recall timer for next frame
		glutTimerFunc(msecsPerFrame, animate, 0);
	}
	else
	{
		//cout << "Stopwatch Camera = " << (stopwatch - msecsPerFrame * 2) / 1000 << "\n"; // Display final time not counting first and last frame
		
		// Must be here to setup next animation call
		isFirstEntry = true;

		glutPostRedisplay();
	}
}
/*-----------------------------------------------*/
static void drawInterpolants()
{
	/* PURPOSE:		Positions and rotates Interpolated dominos in spline
	*/

	float totalTime = 1.0;
	float timeSegment = totalTime / (g_numOfInterpolantDominos + 1);
	float currentTime = timeSegment;
	int dominoIndex = g_numOfControlPoints;
	int startIndex = dominoIndex;

	// Do once for each of control points interpolated between
	for (int i = 1; i < g_numOfControlPoints - 3; i++)
	{
		//cout << "i = " << i << endl;
		while (currentTime < totalTime)
		{
			// Find and set position
			Cvec3 position = catmullRomSpline::interpolate(g_splineArray, i, currentTime);
			g_rigidBodies[dominoIndex].rtf.setTranslation(position);
			g_rigidBodies[dominoIndex].isChildVisible = true;

			// Set Rotation based on first derivative
			Cvec3 screen = Cvec3(0,0,1);
			Cvec3 derivedVector = catmullRomSpline::firstDerivative();
			
			//cout << "Vector        = <" << vector[0] << ", " << vector[1] << ", " << vector[2] << ">" << endl;
			//cout << "DerivedVector Before = <" << derivedVector[0] << ", " << derivedVector[1] << ", " << derivedVector[2] << ">" << endl;
			//cout << "beforeAngle = " << angle << endl;

			//cout << "DerivedVector = <" << derivedVector[0] << ", " << derivedVector[1] << ", " << derivedVector[2] << ">" << endl;

			float angle = angleBetween(derivedVector, screen);

			//cout << "angle = " << angle << endl;

			//if (derivedVector[0] < 0)
			//	angle *= -1;

			//cout << "afterAngle = " << angle << endl;

			g_rigidBodies[dominoIndex].rtf.setRotation(Quat().makeYRotation(angle));
			
			//cout << "derivedVector= <" << derivedVector[0]  << ", " << derivedVector[1] << ", " << derivedVector[2] << ">" << endl;

			dominoIndex++;
			currentTime += timeSegment;
		}
		currentTime = timeSegment;
	}
}
/*-----------------------------------------------*/
static void mouse(const int button, const int state, const int x, const int y) {
  g_mouseClickX = x;
  g_mouseClickY = g_windowHeight - y - 1;  // conversion from GLUT window-coordinate-system to OpenGL window-coordinate-system

  g_mouseLClickButton |= (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN);
  g_mouseRClickButton |= (button == GLUT_RIGHT_BUTTON && state == GLUT_DOWN);
  g_mouseMClickButton |= (button == GLUT_MIDDLE_BUTTON && state == GLUT_DOWN);

  g_mouseLClickButton &= !(button == GLUT_LEFT_BUTTON && state == GLUT_UP);
  g_mouseRClickButton &= !(button == GLUT_RIGHT_BUTTON && state == GLUT_UP);
  g_mouseMClickButton &= !(button == GLUT_MIDDLE_BUTTON && state == GLUT_UP);

  g_mouseClickDown = g_mouseLClickButton || g_mouseRClickButton || g_mouseMClickButton;
}
/*-----------------------------------------------*/
static void keyboard(const unsigned char key, const int x, const int y) 
{
	/* PURPOSE:		OpenGL Callback for Keyboard presses 
		RECEIVES:	unsigned char key - Key pressed
						int x - Mouse x_position when key pressed
						int y - Mouse y_position when key pressed
		REMARKS:		Handles robot modifications based on key presses and then requests a redisplay
	*/

	if (isKeyboardActive)
	{
		switch (key) 
		{
			case 27:
				exit(0);                                  // ESC
			case 'h':
				cout << " ============== H E L P ==============\n\n"
				<< "h\t\thelp menu\n"
				<< "s\t\tsave screenshot\n"
				<< "f\t\tToggle flat shading on/off.\n"
				<< "o\t\tCycle object to edit\n"
				<< "v\t\tCycle view\n"
				<< "drag left mouse to rotate\n" << endl;
				break;
			case 's':
				glFlush();
				writePpmScreenshot(g_windowWidth, g_windowHeight, "out.ppm");
				break;
			case 'f':
				g_activeShader ^= 1;
				break;
	  }

		if (key == '1')
		{
			g_framesPerSecond = 32;
		}
		else if (key == '2')
		{
			g_framesPerSecond = 16;
		}
		else if (key == '3')
		{
			g_framesPerSecond = 8;
		}
		else if (key == '4')
		{
			g_framesPerSecond = 4;
		}
		else if (key == '5')
		{
			g_framesPerSecond = 2;
		}
		else if (key == '6')
		{
			g_framesPerSecond = 1;
		}
		else if (key == '7')
		{
			g_framesPerSecond = 0.5;
		}
		else if (key == '8')
		{
			g_framesPerSecond = 0.25;
		}
		else if (key == '9')
		{
			g_framesPerSecond = 0.125;
		}
	
		if (key == 'i')
		{
			drawInterpolants();
		}
		else if (key == 'a')
		{
			// if hitting the a key draws the interpolants, then starting
			//with the first domino, topples the dominoes over as in a
			//domino effect demonstration.
		}
		// TODO Remove at the End
		else if (key == '-')
		{
			float max = 20;
			Cvec3 cameraTrans = g_eyeRbt.getTranslation();

			g_eyeRbt.setTranslation(cameraTrans + Cvec3(0,0,1));
		
			if (cameraTrans[2] >= max)
				g_eyeRbt.setTranslation(Cvec3(cameraTrans[0], cameraTrans[1], max));

			lookAtOrigin();
		}
		// TODO Remove at the End
		else if (key == '=')
		{
			float min = 5;
			Cvec3 cameraTrans = g_eyeRbt.getTranslation();

			g_eyeRbt.setTranslation(cameraTrans - Cvec3(0,0,1));
	
			if (cameraTrans[2] <= min)
				g_eyeRbt.setTranslation(Cvec3(cameraTrans[0], cameraTrans[1], min));

			lookAtOrigin();
		}
	}

	glutPostRedisplay();
}
/*-----------------------------------------------*/
static void initGlutState(int argc, char * argv[]) {
  glutInit(&argc, argv);                                  // initialize Glut based on cmd-line args
  glutInitDisplayMode(GLUT_RGBA|GLUT_DOUBLE|GLUT_DEPTH);  //  RGBA pixel channels and double buffering
  glutInitWindowSize(g_windowWidth, g_windowHeight);      // create a window
  glutCreateWindow("Assignment 2");                       // title the window

  glutDisplayFunc(display);                               // display rendering callback
  glutReshapeFunc(reshape);                               // window reshape callback
  glutMotionFunc(motion);                                 // mouse movement callback
  glutMouseFunc(mouse);                                   // mouse click callback
  glutKeyboardFunc(keyboard);
}

static void initGLState() {
  glClearColor(128./255., 200./255., 255./255., 0.);
  glClearDepth(0.);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glCullFace(GL_BACK);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_GREATER);
  glReadBuffer(GL_BACK);
  if (!g_Gl2Compatible)
    glEnable(GL_FRAMEBUFFER_SRGB);
}

static void initShaders() {
  g_shaderStates.resize(g_numShaders);
  for (int i = 0; i < g_numShaders; ++i) {
    if (g_Gl2Compatible)
      g_shaderStates[i].reset(new ShaderState(g_shaderFilesGl2[i][0], g_shaderFilesGl2[i][1]));
    else
      g_shaderStates[i].reset(new ShaderState(g_shaderFiles[i][0], g_shaderFiles[i][1]));
  }
}

static void initGeometry() 
{
	//Initialize Object Matrix array
	initDominos();
	initGround();

}

static void initSplines()
{
	g_splineArray = splineReader::parseSplineFile("spline.txt", &g_numOfControlPoints);
}

int main(int argc, char * argv[]) {
  try {
		initGlutState(argc,argv);

		glewInit(); // load the OpenGL extensions

		cout << (g_Gl2Compatible ? "Will use OpenGL 2.x / GLSL 1.0" : "Will use OpenGL 3.x / GLSL 1.3") << endl;
		if ((!g_Gl2Compatible) && !GLEW_VERSION_3_0)
		throw runtime_error("Error: card/driver does not support OpenGL Shading Language v1.3");
		else if (g_Gl2Compatible && !GLEW_VERSION_2_0)
		throw runtime_error("Error: card/driver does not support OpenGL Shading Language v1.0");

		initGLState();
		initShaders();
		initSplines();
		initGeometry();
		initCamera();

/*		
		//Debug stuff
		cout << "\n";
		Matrix4::print(g_skyRbt);
		cout << "\n";
*/

		//glutTimerFunc(0, timer, -1);

		glutMainLoop();
		return 0;
  }
  catch (const runtime_error& e) {
    cout << "Exception caught: " << e.what() << endl;
    return -1;
  }
}

// TODO Remove at end
/*-----------------------------------------------*/

	/* PURPOSE:		What does this function do? (must be present) 
		RECEIVES:   List every argument name and explain each argument. 
						(omit if the function has no arguments) 
		RETURNS:    Explain the value returned by the function. 
						(omit if the function returns no value) 
		REMARKS:    Explain any special preconditions or postconditions. 
						See example below. (omit if function is unremarkable) 
	*/