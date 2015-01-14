// sample of editor data
// this example uses jbin::JBIterator
// there is no guarantee that this sample really does what it claims

//
// This sample is intended to be used with JB_KEY_HASH and JB_KEY_HASH_AND_NAME
//
// Requires both JSONBin and JSONOut
//

//=========================================================================
// Coding style is not representative of a real product, it is kept simple
//      to improve readability and avoid confusing dependencies.
//	 Error checking is unforgiving and silent to keep things simple.
//=========================================================================

//
// Randomly generates a simplistic scene graph, saves it and then loads it.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <time.h>
#include "../jsonbin/jsonbin.h"
#include "../jsonout/jsonout.h"

#ifdef WIN32
#define no_warn_strdup _strdup
#define snprintf sprintf_s
#else
#define no_warn_strdup strdup
#endif

// hashed strings
#define _FNV1A_MATRIX 0x15c2f8ec        // "matrix"
#define _FNV1A_COMPONENTS 0x1bf51169    // "components"
#define _FNV1A_Geo 0xbf974d22			// "Geo"
#define _FNV1A_PathFollow 0x572376a3    // "PathFollow"
#define _FNV1A_Character 0xec7340b0     // "Character"
#define _FNV1A_name 0x8d39bde6			// "name"
#define _FNV1A_geoFile 0x446b6c64       // "geoFile"
#define _FNV1A_wayPoints 0x4d43f60b     // "wayPoints"
#define _FNV1A_behavior 0xcfe9be27      // "behavior"
#define _FNV1A_spawnPoint 0x02a82a2e    // "spawnPoint"
#define _FNV1A_scene 0x2063cb13			// "scene"
#define _FNV1A_objects 0xa8c6206b       // "objects"

inline float randFloat(float range_min, float range_max)
{
	return float(rand()) * (range_max - range_min) / float(RAND_MAX) + range_min;
}

inline int randInt(int range_min, int range_max)
{
    // dealing with Win32 rand() is 16 bit and OSX rand() is 32 bit and want larger random by modulo
    unsigned int rand2 = (unsigned int)rand() + (unsigned int)rand() * ((unsigned int)RAND_MAX+1);
    return (int)(rand2%(unsigned int)(range_max+1-range_min))+range_min;
}

struct SceneVec {
	float x, y, z;
	SceneVec() {}
	SceneVec(float _x, float _y, float _z) { x = _x; y = _y; z = _z; }
	float len() { return sqrtf(x*x+y*y+z*z); }
	void normalize() { float il = 1.0f/len(); x *= il; y *= il; z *= il; }
	void scale(float s) { x*=s; y*=s; z*=s; }
	void random(float r=1.0f) { x=randFloat(-r,r); y=randFloat(-r,r); z=randFloat(-r,r);}
	void clear() { x = 0.0f; y = 0.0f; z = 0.0f; }
	SceneVec& operator+=(const SceneVec &v) { x += v.x; y += v.y; z += v.z; return *this; }

	bool Load(const jbin::JBItem *);
	void Save(jout::JSONOut *o);
};

struct SceneMatrix {
	SceneVec x, y, z, t;

	bool Load(const jbin::JBItem *);
	void Save(jout::JSONOut *o, const char *name);
};

class SceneComponent {
public:
	SceneComponent *pNext;
	const char *pName;

	SceneComponent() : pNext(NULL), pName(NULL) {}

	virtual bool Load(const jbin::JBItem *);
	virtual void Save(jout::JSONOut *o);

	virtual const char* TypeName() { return "N/A"; }
	const char* InstanceName() { return pName; }
	virtual ~SceneComponent() { if (pName) free((void*)pName);  }
};

struct SceneObject {
	SceneObject *pNext;
	SceneMatrix m_mat;
	unsigned int id;
	const char *name;
	SceneComponent *pComponents;

	SceneObject() : pNext(NULL), name(NULL), pComponents(NULL) {}

	bool Load(const jbin::JBItem *);
	void Save(jout::JSONOut *o);

	~SceneObject() {
		if (name) free((void*)name);
		for (SceneComponent *pComp = pComponents; pComp;) {
			SceneComponent *c = pComp->pNext; delete pComp; pComp = c;
		}
	}

};

struct Scene {
	SceneObject *pObjects;

	Scene() : pObjects(NULL) {}

	bool Load(const jbin::JBItem *);
	void Save(jout::JSONOut *o);

	~Scene() { for (SceneObject *pObj = pObjects; pObj;) { SceneObject *n = pObj->pNext; delete pObj; pObj = n; } }
};

class SceneGeo : public SceneComponent {
public:
	const char *pGeoFileName;
	bool Load(const jbin::JBItem *);
	void Save(jout::JSONOut *o);
	const char* TypeName() { return "Geo"; }

	SceneGeo() : pGeoFileName(NULL) {}
	~SceneGeo() { if (pGeoFileName) free((void*)pGeoFileName); }
};

class ScenePathFollow : public SceneComponent {
public:
	SceneVec *pPoints;
	int nPoints;
	bool Load(const jbin::JBItem *);
	void Save(jout::JSONOut *o);
	const char* TypeName() { return "Geo"; }

	ScenePathFollow() : pPoints(NULL), nPoints(0) {}
	~ScenePathFollow() { if (pPoints) free(pPoints); }
};

class SceneCharacter : public SceneComponent {
public:
	const char *pBehaviorScript;
	SceneVec spawnPoint;
	bool Load(const jbin::JBItem *);
	void Save(jout::JSONOut *o);
	const char* TypeName() { return "Geo"; }

	SceneCharacter() : pBehaviorScript(NULL) { spawnPoint.clear();  }
};

SceneVec cross(const SceneVec &v1, const SceneVec &v2) {
	return SceneVec(v1.y*v2.z-v1.z-v2.y, v1.z*v2.x-v1.x*v2.z, v1.x*v2.y-v1.y*v2.x);
}

static void RandomMatrix(SceneMatrix &mat)
{
	mat.x.random();
	mat.x.normalize();
	SceneVec z;
	z.random();
	mat.y = cross(z,mat.x);
    mat.y.normalize();
	mat.z = cross(mat.x, mat.y);
	mat.t.random();
	mat.t.normalize();
	mat.t.scale(400.0f);
}

static int RandomName(char *buf, int buf_size)
{
	int len = randInt(1, buf_size-1);

    *buf++ = randInt('A', 'Z');

	for (int c = 1; c < len; c++)
        *buf++ = randInt('a', 'z');

	*buf++ = 0;
	return len;
}


bool SceneVec::Load(const jbin::JBItem *pJSON)
{
	if (pJSON->getType() == jbin::JB_ARRAY && pJSON->getChildCount() == 3) {
		float *ptr = &x;
		for (jbin::JBIterator v = pJSON->begin(); v.valid(); ++v)
			*ptr++ = (float)v->getFloat();
		return true;
	}
	return false;
}

void SceneVec::Save(jout::JSONOut *o)
{
	o->element(x);
	o->element(y);
	o->element(z);
}

bool SceneMatrix::Load(const jbin::JBItem *pJSON)
{
	if (pJSON->getType() == jbin::JB_ARRAY && pJSON->getChildCount()==4) {
		SceneVec *ptr = &x;
		for (jbin::JBIterator m = pJSON->begin(); m.valid(); ++m) {
			if (!ptr->Load(*m))
				return false;
		}
		return true;
	}
	return false;
}

void SceneMatrix::Save(jout::JSONOut *o, const char *name)
{
	o->push_array(name);
	SceneVec *ptr = &x;
	for (int c = 0; c < 4; c++) {
		o->element_array();
		ptr->Save(o);
		o->scope_end();
		ptr++;
	}
	o->scope_end();
}


bool SceneComponent::Load(const jbin::JBItem *pJSON)
{
	if (const jbin::JBItem *pNameSrc = pJSON->findByHash(_FNV1A_name)) {
		pName = no_warn_strdup(pNameSrc->getStr());
		return true;
	}
	return false;
}

void SceneComponent::Save(jout::JSONOut *o)
{
	if (pName)
		o->push("name", pName);
}

bool SceneGeo::Load(const jbin::JBItem *pJSON)
{
	SceneComponent::Load(pJSON);
	if (const jbin::JBItem *pNameSrc = pJSON->findByHash(_FNV1A_geoFile)) {
		pGeoFileName = no_warn_strdup(pNameSrc->getStr());
	}
	return false;
}

void SceneGeo::Save(jout::JSONOut *o)
{
	o->push_object("Geo");
	SceneComponent::Save(o);
	if (pGeoFileName)
		o->push("geoFile", pGeoFileName);
	o->scope_end();
}

bool ScenePathFollow::Load(const jbin::JBItem *pJSON)
{
	SceneComponent::Load(pJSON);
	if (const jbin::JBItem *pPointsSrc = pJSON->findByHash(_FNV1A_wayPoints)) {
		if ((nPoints = (int)pPointsSrc->getChildCount())) {
			pPoints = new SceneVec[nPoints];
			SceneVec *ptr = pPoints;
			for (jbin::JBIterator i = pPointsSrc->begin(); i.valid(); ++i) {
				ptr->Load(*i);
				ptr++;
			}
		}
	}
	return false;
}

void ScenePathFollow::Save(jout::JSONOut *o)
{
	o->push_object("PathFollow");
	SceneComponent::Save(o);
	if (pPoints) {
		o->push_array("wayPoints");
		for (int p = 0; p < nPoints; p++) {
			o->element_array();
			pPoints[p].Save(o);
			o->scope_end();
		}
		o->scope_end();
	}
	o->scope_end();
}

bool SceneCharacter::Load(const jbin::JBItem *pJSON)
{
	SceneComponent::Load(pJSON);

	if (const jbin::JBItem *pBehavior = pJSON->findByHash(_FNV1A_behavior))
		pBehaviorScript = no_warn_strdup(pBehavior->getStr());

	if (const jbin::JBItem *pSpawn = pJSON->findByHash(_FNV1A_spawnPoint))
		spawnPoint.Load(pSpawn);

	return false;
}

void SceneCharacter::Save(jout::JSONOut *o)
{
	o->push_object("Character");

	SceneComponent::Save(o);

	if (pBehaviorScript)
		o->push("behavior", pBehaviorScript);

	o->push_array("spawnPoint");
	spawnPoint.Save(o);
	o->scope_end();

	o->scope_end();
}

static SceneComponent* LoadComponent(const jbin::JBItem *pJSON)
{
	switch (pJSON->getHash()) {
		case _FNV1A_Geo: {			// "Geo"
			SceneGeo *pGeo = new SceneGeo();
			pGeo->Load(pJSON);
			return pGeo;
		}
		case _FNV1A_PathFollow: {    // "PathFollow"
			ScenePathFollow *pPathFollow = new ScenePathFollow();
			pPathFollow->Load(pJSON);
			return pPathFollow;
		}
		case _FNV1A_Character: {     // "Character"
			SceneCharacter *pCharacter = new SceneCharacter();
			pCharacter->Load(pJSON);
			return pCharacter;
		}
	}
	return NULL;
}

bool SceneObject::Load(const jbin::JBItem *pJSON)
{
	bool fail = false;
	name = no_warn_strdup(pJSON->getName());
	for (jbin::JBIterator i = pJSON->begin(); !fail && i.valid(); ++i) {
		switch (i->getHash()) {
			case _FNV1A_MATRIX: {
				fail = !m_mat.Load(*i);
				break;
			}
			case _FNV1A_COMPONENTS:
				if (i->getType() == jbin::JB_OBJECT) {
					SceneComponent **ppNext = &pComponents;
					for (jbin::JBIterator c = i->begin(); !fail && c.valid(); ++c) {
						if (SceneComponent *pComponent = LoadComponent(*c)) {
							pComponent->pNext = *ppNext;
							*ppNext = pComponent;
							ppNext = &pComponent->pNext;
						} else
							fail = true;
					}
				} else
					fail = true;
				break;
		}
	}
	return !fail;
}

void SceneObject::Save(jout::JSONOut *o)
{
	o->push_object(name);
	m_mat.Save(o, "matrix");
	o->push_object("components");
	for (SceneComponent *pComp = pComponents; pComp; pComp = pComp->pNext)
		pComp->Save(o);
	o->scope_end();
	o->scope_end();
}

bool Scene::Load(const jbin::JBItem *pJSON)
{
	if (const jbin::JBItem *pSceneItem = pJSON->findByHash(_FNV1A_scene)) {
		if (const jbin::JBItem *pObjectsItem = pSceneItem->findByHash(_FNV1A_objects)) {
			SceneObject **ppIns = &pObjects;
			for (jbin::JBIterator i = pObjectsItem->begin(); i != pObjectsItem->end(); ++i) {
				SceneObject *pObj = new SceneObject;
				if (pObj->Load(*i)) {
					pObj->pNext = *ppIns;
					*ppIns = pObj;
					ppIns = &pObj->pNext;
				} else {
					delete (pObj);	// load error
					return false;
				}
			}
			return true;
		}
	}
	return false;
}

void Scene::Save(jout::JSONOut *o)
{
	o->push_object("scene");

	o->push_object("objects");
	for (SceneObject *pObj = pObjects; pObj; pObj = pObj->pNext)
		pObj->Save(o);
	o->scope_end();

	o->scope_end();
}


static SceneComponent* RandomComponent(SceneObject *pObject)
{
	char name[64];
	int o = RandomName(name, 12);
	snprintf(name + o, sizeof(name) - o, "_%03d", randInt(0,256));

	switch (randInt(0,3)) {
		case 0: {
			char file[64];
			int n = RandomName(file, 8);
			file[n++] = '/';
			n += RandomName(file + n, 8);
			snprintf(file + n, sizeof(file) - n, ".mdl");
			SceneGeo *pGeo = new SceneGeo();
			pGeo->pNext = NULL;
			pGeo->pName = no_warn_strdup(name);
			pGeo->pGeoFileName = no_warn_strdup(file);
			return pGeo;
		}

		case 1: {
			ScenePathFollow *pPF = new ScenePathFollow;
			pPF->pNext = NULL;
			pPF->pName = no_warn_strdup(name);
			pPF->nPoints = randInt(4, 64);
			pPF->pPoints = new SceneVec[pPF->nPoints];
			SceneVec delta;
			SceneVec pos;
			pos = pObject->m_mat.t;
			delta.random(randFloat(1.0f, 10.0f));
			for (int p = 0; p < pPF->nPoints; p++) {
				pPF->pPoints[p] = pos;
				SceneVec change;
				change.random(randFloat(0.5f,2.0f));
				delta += change;
				pos += delta;
			}
			return pPF;
		}

		default: {
			SceneCharacter *pChar = new SceneCharacter;
			pChar->pName = NULL;
			pChar->pName = no_warn_strdup(name);
			pChar->spawnPoint.random(256.0f);
			char behavior[64];
			RandomName(behavior, 16);
			pChar->pBehaviorScript = no_warn_strdup(behavior);
			return pChar;
		}
	}
	return NULL;
}

static SceneObject* RandomObject()
{
	char randName[256];
	SceneObject *pRet = new SceneObject;
	RandomMatrix(pRet->m_mat);
	pRet->pComponents = NULL;
	
	int o = RandomName(randName, 12);
	snprintf(randName + o, sizeof(randName) - o, "_%03d", randInt(1, 256));

	pRet->name = no_warn_strdup(randName);

	int numComponents = randInt(1, 3);
	SceneComponent **ppInsert = &pRet->pComponents;
	for (int c = 0; c < numComponents; c++) {
		if (SceneComponent *pComp = RandomComponent(pRet)) {
			pComp->pNext = *ppInsert;
			*ppInsert = pComp;
			ppInsert = &pComp->pNext;
		}
	}
	return pRet;
}

static Scene* RandomScene()
{
	Scene *pScene = new Scene;
	pScene->pObjects = NULL;

	int numObjects = randInt(16, 128*1024);	// generate between 2^5 and 2^24 objects
	SceneObject **ppIns = &pScene->pObjects;
	for (int o = 0; o < numObjects; o++) {
		if (SceneObject *pObj = RandomObject()) {
			pObj->pNext = *ppIns;
			*ppIns = pObj;
			ppIns = &pObj->pNext;
		}
	}
	return pScene;
}

void SceneGraphTest(const char *filename)
{
	srand((unsigned int)time(NULL));
	Scene *pScene = RandomScene();
	FILE *f = 0;
#ifdef WIN32
	if (!fopen_s(&f, filename, "w")) {
#else
	if ((f = fopen(filename, "w"))) {
#endif
		jout::JSONOut o(f);

		pScene->Save(&o);

		o.finish();
		fclose(f);

		// attempt to load test file as a new scene...
		unsigned int size = 0;
		void *data = 0;
#ifdef WIN32
		if (!fopen_s(&f, filename, "rb"))
#else
		if ((f = fopen(filename, "rb")))
#endif
		{
			fseek(f, 0, SEEK_END);
			size = (unsigned int)ftell(f);
			fseek(f, 0, SEEK_SET);
			if ((data = (void*)malloc(size)))
				fread(data, size, 1, f);
			fclose(f);
		}
		if (data) {
			// parse the JSON and discard the file data
			jbin::JBItem *pJSON = jbin::JSONBin((const char*)data, (unsigned int)size);
			free(data);

			// Create a loaded version of the saved scene
			Scene *pLoadedScene = new Scene;
			pLoadedScene->Load(pJSON);
			free(pJSON);

			// Destroy the loaded scene
			delete pLoadedScene;
		}

		// Destroy the generated scene
		delete pScene;
	}
}

int main(int argc, char **argv)
{
	SceneGraphTest("../samples/scene.json");
	return 0;
}