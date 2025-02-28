/* This file is part of COVISE.

   You can use it under the terms of the GNU Lesser General Public License
   version 2.1 or later, see lgpl-2.1.txt.

 * License: LGPL 2+ */

#include "coVRFileManager.h"
#include <config/CoviseConfig.h>
#include "coHud.h"
#include <cassert>
#include <cstring>
#include <cctype>
#include <stdlib.h>

#include <osg/Texture2D>
#include <osgText/Font>
#include <util/unixcompat.h>
#include <util/coFileUtil.h>
#include <util/coHashIter.h>
#include "OpenCOVER.h"
#include "VRSceneGraph.h"
#include "VRViewer.h"
#include "coVRMSController.h"
#include "coVRPluginSupport.h"
#include "coVRPluginList.h"
#include "coVRCommunication.h"
#include "coTabletUI.h"
#include "coVRIOReader.h"
#include "VRRegisterSceneGraph.h"
#include "coVRConfig.h"
#include "coVRRenderer.h"
#include <util/string_util.h>
#include "ui/Owner.h"
#include "ui/Action.h"
#include "ui/Button.h"
#include "ui/Group.h"
#include "ui/FileBrowser.h"
#include "ui/Menu.h"

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <net/message.h>
#include <vrb/client/VRBClient.h>
#include <fcntl.h>



#ifdef __DARWIN_OSX__
#include <Carbon/Carbon.h>
#endif

// undef this to get no START/END messaged
#undef VERBOSE

#ifdef VERBOSE
#define START(msg) fprintf(stderr, "START: %s\n", (msg))
#define END(msg) fprintf(stderr, "END:   %s\n", (msg))
#else
#define START(msg)
#define END(msg)
#endif

using namespace covise;
namespace fs = boost::filesystem;
namespace opencover
{

coVRFileManager *coVRFileManager::s_instance = NULL;


Url::Url(const std::string &url)
{
    if (url.empty())
        return;

    auto it = url.begin();
    // must contain scheme and must begin with an aplhabet character
    if (!isalpha(*it))
        return;

    for ( ; it != url.end(); ++it)
    {
        if (std::isalnum(*it))
            continue;
        if (*it == '+')
            continue;
        if (*it == '-')
            continue;
        if (*it == '.')
            continue;
        if (*it == ':')
            break;

        // weird character in URL scheme
        return;
    }
    if (it == url.end())
        return;
#ifdef WIN32
    // probably just a drive letter, not a URL scheme
    if (it - url.begin() <= 1)
        return;
#endif

    m_scheme = std::string(url.begin(), it);
    ++it;

    int numSlash = 0;
    auto authorityBegin = it;
    for ( ; it != url.end(); ++it)
    {
        if (numSlash >= 2)
            break;
        if (*it != '/')
            break;
        ++numSlash;
    }
    if (numSlash >= 2)
    {
        m_haveAuthority = true;
        auto slash = std::find(it, url.end(), '/');
        auto question = std::find(it, url.end(), '?');
        auto hash = std::find(it, url.end(), '#');
        auto end = slash;
        if (hash < end)
            end = hash;
        if (question < end)
            end = question;
        m_authority = decode(std::string(it, end));
        it = end;
    }
    else
    {
        // no authority
        it = authorityBegin;
    }

    auto question = std::find(it, url.end(), '?');
    auto hash = std::find(it, url.end(), '#');
    if (question == url.end())
    {
        m_path = decode(std::string(it, hash), true);
    }
    else
    {
        m_path = decode(std::string(it, question), true);
        it = question;
        ++it;
        hash = std::find(it, url.end(), '#');
        m_query = decode(std::string(it, hash));
    }
    it = hash;
    if (it != url.end())
        ++it;
    m_fragment = decode(std::string(it, url.end()));

    m_valid = true;
}

Url Url::fromFileOrUrl(const std::string &furl)
{
    Url url(furl);
    if (url.valid())
        return url;

    Url file;
    file.m_scheme = "file";
    file.m_path = furl;
    file.m_valid = true;
    return file;
}

std::string Url::decode(const std::string &str, bool path)
{
    return url_decode(str, path);
}

std::string Url::str() const
{
    if (!valid())
        return "(invalid)";

    if (scheme() == "file")
        return path();

    std::string str = scheme();
    str += ":";
    if (m_haveAuthority)
    {
        str += "//";
        str += authority();
    }
    str += path();
    if (!m_query.empty())
    {
        str += "?";
        str += m_query;
    }
    if (!m_fragment.empty())
    {
        str += "#";
        str += m_fragment;
    }

    return str;
}

std::string Url::extension() const
{
    if (!m_valid)
        return std::string();
    if (m_path.empty())
        return std::string();
    auto pos = m_path.find_last_of('.');
    if (pos == std::string::npos)
        return std::string();
    return m_path.substr(pos);
}

bool Url::valid() const
{
    return m_valid;
}

bool Url::isLocal() const
{
    return scheme() == "file";
}

const std::string &Url::scheme() const
{
    return m_scheme;
}

const std::string &Url::authority() const
{
    return m_authority;
}

const std::string &Url::path() const
{
    return m_path;
}

const std::string &Url::query() const
{
    return m_query;
}

const std::string &Url::fragment() const
{
    return m_fragment;
}

Url::Url()
{
}

std::ostream &operator<<(std::ostream &os, const Url &url)
{
    os << url.str();
    return os;
}

std::string shortenUrl(const Url &url, size_t length=20)
{
    return url.str();
}

struct LoadedFile: public osg::Observer
{
    LoadedFile(const Url &url, ui::Button *button=nullptr, bool isRoot=false)
        : url(url)
        , button(button)
        , isRoot(isRoot)
    {
        if  (button)
        {
            auto n = coVRFileManager::instance()->getFileName(url.str());
            if (n.length() == 0)
            {
                n = url.str();
            }
            button->setState(false);
            button->setCallback([this](bool state){
              setVisible(state);
              updateButton();
            });
            updateButton();
        }
    }
    
    ~LoadedFile()
    {
        if (cover->debugLevel(3)) {
            if (node)
                std::cerr << "coVRFileManager: LoadedFile: destroy " << url.str() << ", refcount=" << node->referenceCount() << std::endl;
            else
                std::cerr << "coVRFileManager: LoadedFile: destroy " << url.str() << ", NO NODE" << std::endl;
        }
    }

    void objectDeleted(void *d) override
    {
        auto *deleted = static_cast<osg::Referenced *>(d);
        std::cerr << "coVRFileManager: LoadedFile: parent of " << url.str() << " deleted" << std::endl;
        parents.erase(static_cast<osg::Group *>(deleted));
    }

    Url url;
    std::shared_ptr<ui::Button> button;

    bool isRoot = false;
    std::string key;
    osg::ref_ptr<osg::Node> node;
    std::set<osg::Group *> parents;
    const FileHandler *handler = nullptr;
    coVRIOReader *reader = nullptr;
    coTUIFileBrowserButton *filebrowser = nullptr;
    int loadCount = 0;

    int numInstances() const
    {
        return loadCount;
    }

    void updateButton() {
        if (!button)
            return;

        auto n = coVRFileManager::instance()->getFileName(url.str());
        if (n.length() == 0)
        {
            n = url.str();
        }
        //std::cerr << "updateButton: loadCount=" << loadCount << ", #parents=" << parents.size() << std::endl;
        if (numInstances() > 1)
            n += " (" + std::to_string(numInstances()) + "x)";
        button->setText(n);
    }

    void setVisible(bool state)
    {
        if (state) {
            if (node)
            {
                for (const auto &p : parents) {
                    p->addChild(node);
                    p->removeObserver(this);
                }
            }
            parents.clear();
        } else {
            while (node && node->getNumParents() > 0) {
                unsigned i = node->getNumParents() - 1;
                auto p = node->getParent(i);
                parents.emplace(p);
                p->addObserver(this);
                p->removeChild(node);
            }
        }
    }

    osg::Node *load();

    osg::Node *reload()
    {
        std::vector<osg::ref_ptr<osg::Group>> parents;
        while (node && node->getNumParents() > 0) {
            unsigned i = node->getNumParents() - 1;
            auto p = node->getParent(i);
            parents.emplace_back(p);
            p->removeChild(node);
        }

        int lc = loadCount;
        while (loadCount > 0)
            unload();
        assert(node == nullptr);
        node = nullptr;
        while (loadCount < lc)
            node = load();

        for (const auto &p : parents) {
            p->addChild(node);
        }

        return node;
    }

  bool unload()
  {
      if (loadCount <= 0)
          return false;
      --loadCount;
      if (loadCount > 0)
          return true;

      const char *ck = nullptr;
      if (!key.empty())
          ck = key.c_str();

      bool ok = false;
      if (handler)
      {
          if (handler->unloadFile)
          {
              ok = handler->unloadFile(url.str().c_str(), ck) == 0;
          }
         // if (node)
           //   node.release();
      }
      else if (reader)
      {
          ok = reader->unload(node);
      }
      else if (node)
      {
          //parent->removeChild(node);
          //node.release();

          ok = true;
      }

      while (node)
      {
          unsigned n = node->getNumParents();
          if (n == 0)
              break;
          node->getParent(n-1)->removeChild(node);
      }
      if (cover->debugLevel(3)) {
          if (node)
              std::cerr << "unload: node removed from all parents, refcount="
                        << node->referenceCount() << std::endl;
          else
              std::cerr << "unload: node is NULL" << std::endl;
      }
      node = nullptr;

      return ok;
  }
};

osg::Node *LoadedFile::load()
{
    if (node) {
        ++loadCount;
        return node;
    }

    const char *covise_key = nullptr;
    if (!key.empty())
        covise_key = key.c_str();

    auto adjustedFileName = url.str();
    auto &fb = filebrowser;

    osg::ref_ptr<osg::Group> fakeParent = new osg::Group;
    if (handler)
    {
        if (cover->debugLevel(3))
            fprintf(stderr, "coVRFileManager::loadFile(name=%s)   handler\n", url.str().c_str());
        if (handler->loadUrl)
        {
			if (handler->loadUrl(url, fakeParent, covise_key) < 0)
			{
				if (button)
					button->setState(false);
			}
        }
        else
        {
            if (fb)
            {
                std::string tmpFileName = fb->getFilename(adjustedFileName);
                if (tmpFileName == "")
                    tmpFileName = adjustedFileName;
                std::cerr << "fb: tmpFileName=" << tmpFileName << std::endl;

                if (handler->loadFile)
                    handler->loadFile(tmpFileName.c_str(), fakeParent, covise_key);
                coVRCommunication::instance()->setCurrentFile(tmpFileName.c_str());
            }
            else
            {
                if (handler->loadFile)
                    handler->loadFile(adjustedFileName.c_str(), fakeParent, covise_key);
                coVRCommunication::instance()->setCurrentFile(adjustedFileName.c_str());
            }
        }
    }
    else if (reader)
    {
        //fprintf(stderr, "coVRFileManager::loadFile(name=%s)  reader\n", fileName);
        std::string filenameToLoad = adjustedFileName;

        if (fb)
        {
            filenameToLoad = fb->getFilename(adjustedFileName);
            if (filenameToLoad == "")
                filenameToLoad = adjustedFileName;
        }

        if (reader->canLoadParts())
        {
            if (cover->debugLevel(3))
                std::cerr << "coVRFileManager::loadFile info: loading parts" << std::endl;
            coVRFileManager::IOReadOperation op;
            op.filename = filenameToLoad;
            op.reader = reader;
            op.group = fakeParent;
            coVRFileManager::instance()->readOperations[reader->getIOHandlerName()].push_back(op);
        }
        else
        {
            if (cover->debugLevel(3))
                std::cerr << "coVRFileManager::loadFile info: loading full" << std::endl;
            reader->load(filenameToLoad, fakeParent);
        }

        coVRCommunication::instance()->setCurrentFile(adjustedFileName.c_str());
    }
    else
    {
        //fprintf(stderr, "coVRFileManager::loadFile(name=%s)   else\n", fileName);
        //(fileName,fileTypeString);
        //obj-Objects must not be rotated
       // don't run optimizer on STL and OBJ files

        std::string tmpFileName = adjustedFileName;
        if (fb)
        {
            tmpFileName = fb->getFilename(adjustedFileName);
        }
        node = osgDB::readNodeFile(tmpFileName.c_str(), coVRFileManager::instance()->options.get());
        if (node)
        {
            //OpenCOVER::instance()->databasePager->registerPagedLODs(node);
            coVRCommunication::instance()->setCurrentFile(adjustedFileName.c_str());
            node->setNodeMask(node->getNodeMask() & (~Isect::Intersection));
            if (cover->debugLevel(3))
                fprintf(stderr, "coVRFileManager::loadFile setting nodeMask of %s to %x\n", node->getName().c_str(), node->getNodeMask());
        }
        else
        {
            if (covise::coFile::exists(adjustedFileName.c_str()))
                std::cerr << "WARNING: Could not load file " << adjustedFileName << std::endl;
        }

        //VRViewer::instance()->Compile();
    }

    if (!node)
    {
        auto n = fakeParent->getNumChildren();
        if (n == 1)
        {
            node = fakeParent->getChild(0);
        }
        else if (n > 1)
        {
            osg::Group *g = new osg::Group();
            node = g;
            for (unsigned int i = 0; i < n; ++i)
            {
                g->addChild(fakeParent->getChild(i));
            }
        } 
    }

    if (node && node->getName() == "")
    {
        node->setName(url.str());
    }

    if (isRoot)
        coVRFileManager::instance()->m_loadingFile = nullptr;

    assert(loadCount == 0);
    if (node)
    {
        ++loadCount;
    }

    return node;
}

// load an icon file looks in covise/share/covise/icons/$LookAndFeel or covise/share/covise/icons
// returns NULL, if nothing found
osg::Node *coVRFileManager::loadIcon(const char *filename)
{
    START("coVRFileManager::loadIcon");
    osg::Node *node = NULL;
    if (node == NULL)
    {
        const char *name = NULL;
        std::string look = coCoviseConfig::getEntry("COVER.LookAndFeel");
        if (!look.empty())
        {
            std::string fn = "share/covise/icons/osg/" + look + "/" + filename + ".osg";
            name = getName(fn.c_str());
            if (!name)
            {
                std::string fn = "share/covise/icons/" + look + "/" + filename + ".iv";
                name = getName(fn.c_str());
            }
        }
        if (name == NULL)
        {
            std::string fn = "share/covise/icons/osg/";
            fn += filename;
            fn += ".osg";
            name = getName(fn.c_str());
        }
        if (name == NULL)
        {
            std::string fn = "share/covise/icons/";
            fn += filename;
            fn += ".iv";
            name = getName(fn.c_str());
        }
        if (name == NULL)
        {
            std::string fn = "share/covise/icons/";
            fn += filename;
            fn += ".obj";
            name = getName(fn.c_str());
        }
        if (name == NULL)
        {
            if (cover->debugLevel(4))
                fprintf(stderr, "Did not find icon %s\n", filename);
            return NULL;
        }
        node = osgDB::readNodeFile(name);
        if (node)
            node->setName(filename);
        else
        {
            fprintf(stderr, "Error loading icon %s\n", filename);
        }
    }
    return (node);
}

// parmanently loads a texture, looks in covise/icons/$LookAndFeel or covise/icons for filename.rgb
// returns NULL, if nothing found, reuses textures that have already been loaded
osg::Texture2D *coVRFileManager::loadTexture(const char *texture)
{
    START("coVRFileManager::loadTexture");

    TextureMap::iterator it = textureList.find(texture);
    if (it != textureList.end())
    {
        if (it->second.get())
        {
            if (cover->debugLevel(4))
                std::cerr << "Reusing texture " << texture << std::endl;
            return it->second.get();
        }
    }
    const char *name = buildFileName(texture);
    if (name == NULL)
    {
        if (cover->debugLevel(2))
            std::cerr << "New texture " << texture << " - not found" << std::endl;
        return NULL;
    }
    else
    {
        if (cover->debugLevel(3))
            std::cerr << "New texture " << texture << std::endl;
    }

    osg::Texture2D *tex = new osg::Texture2D();
    osg::Image *image = osgDB::readImageFile(name);
    tex->setImage(image);
    textureList[texture] = tex;

    return tex;
}

const char *coVRFileManager::buildFileName(const char *texture)
{
    const char *name = getName(texture);
    std::string look;
    if (name == NULL)
    {
        look = coCoviseConfig::getEntry("COVER.LookAndFeel");
        if (!look.empty())
        {
            char *fn = new char[strlen(texture) + strlen(look.c_str()) + 50];
            sprintf(fn, "share/covise/icons/%s/%s.rgb", look.c_str(), texture);
            name = getName(fn);
            delete[] fn;
        }
    }
    if (name == NULL)
    {
        char *fn = new char[strlen(texture) + 50];
        sprintf(fn, "share/covise/icons/%s.rgb", texture);
        name = getName(fn);
        delete[] fn;
    }

    return name;
}

bool coVRFileManager::fileExist(const char *fileName)
{
	try
	{
		return fs::exists(fileName);
	}
	catch (const std::exception&)
	{
		return false;
	}
	
}

bool coVRFileManager::fileExist(const std::string& fileName)
{
	return fileExist(fileName.c_str());
}

osg::Node *coVRFileManager::loadFile(const char *fileName, coTUIFileBrowserButton *fb, osg::Group *parent, const char *covise_key)
{
    START("coVRFileManager::loadFile");
    if (!fileName || strcmp(fileName, "") == 0)
    {
        return nullptr;
    }

    if (!parent)
        parent = cover->getObjectsRoot();

	std::string validFileName(fileName);
	convertBackslash(validFileName);

    auto duplicate = m_files.find(validFileName);
    if (duplicate != m_files.end())
    {
        auto node = duplicate->second->load();
        duplicate->second->updateButton();
        parent->addChild(node);
        return node;
    }

    std::string key;
    if (fb)
    {
        //Store filename associated with corresponding fb instance
        key = validFileName;
        fileFBMap[key] = fb;
    }
	//url to local file
    Url url = Url::fromFileOrUrl(validFileName);
    if (!url.valid())
    {
        std::cerr << "failed to parse URL " << validFileName << std::endl;
        return  nullptr;
    }
    if (cover->debugLevel(3))
        std::cerr << "Loading " << url.str() << std::endl;

    std::string adjustedFileName;
    if (url.scheme() == "cover"|| url.scheme() == "opencover")
    {
        if (url.authority() == "plugin")
        {
            coVRPluginList::instance()->addPlugin(url.path().c_str()+1);
        }
        return nullptr;
    }
    else if (url.scheme() == "file")
    {
        adjustedFileName = url.path();
        if (cover->debugLevel(3))
            std::cerr << " New filename: " << adjustedFileName << std::endl;
    }
    else
    {
        adjustedFileName = url.str();
    }


    bool isRoot = m_loadingFile==nullptr;
    ui::Button *button = nullptr;
    if (cover && isRoot)
    {
		std::string relPath(adjustedFileName);
		makeRelativeToSharedDataLink(relPath);
		button = new ui::Button(m_fileGroup, "File" + reduceToAlphanumeric(relPath)+std::to_string(uniqueNumber));
        uniqueNumber++;
    }
	if (isRoot)
	{
		//if file is not shared, add it to the shared filePaths list
		fileOwnerList v = m_sharedFiles;
		std::string pathIdentifier(adjustedFileName);
		makeRelativeToSharedDataLink(pathIdentifier);
		bool found = false;
		for (auto p : v)
		{
			if (p.first == pathIdentifier)
			{
				found = true;
				break;
			}
		}


		if (!found)
		{
			v.push_back(fileOwner(pathIdentifier, coVRCommunication::instance()->getID()));
			m_sharedFiles = v;
		}
	}
    auto fe = new LoadedFile(url, button, isRoot);
    if (covise_key)
        fe->key = covise_key;
    fe->filebrowser = fb;

    OpenCOVER::instance()->hud->setText2("loading");
    OpenCOVER::instance()->hud->setText3(validFileName);
    OpenCOVER::instance()->hud->redraw();

    if (isRoot)
    {
        m_loadingFile = fe;

        if(viewPointFile == "" && url.isLocal())
        {
            const char *ext = strchr(url.path().c_str(), '.');
            if(ext)
            {
                viewPointFile = validFileName;
                std::string::size_type pos = viewPointFile.find_last_of('.');
                viewPointFile = viewPointFile.substr(0,pos);
                viewPointFile+=".vwp";
            }
        }
    }

    /// read the 1st line of file and try to guess the type
    std::string fileTypeString = findFileExt(url);
    const FileHandler *handler = findFileHandler(url.path().c_str());
    coVRIOReader *reader = findIOHandler(adjustedFileName.c_str());
    if (!handler && !fileTypeString.empty())
        handler = findFileHandler(fileTypeString.c_str());
	//vrml will remote fetch missing files itself
	std::string xt = url.extension();
	
	std::vector<std::string> vrmlExtentions{ "x3dv", "wrl", "wrz" };
	std::string lowXt(xt);
	std::transform(xt.begin(), xt.end(), lowXt.begin(), ::tolower);
	bool isVRML = false;
	for (auto ext : vrmlExtentions)
	{
		if (("." + ext) == lowXt)
		{
			isVRML = true;
			break;
		}
	}
	if (!isVRML)
	{
        std::string filename = findOrGetFile(adjustedFileName);
		if (filename.length() == 0)
		{
            delete fe;
			return nullptr;
		}
		fe->url = Url::fromFileOrUrl(filename);

	}
    fe->handler = handler;
    fe->reader = reader;
    
	if (fe->button)
	{
		fe->button->setShared(true);
		fe->button->setState(true);
	}

    auto node = fe->load();
    fe->updateButton();
    if (node != NULL)
    {
        fe->filebrowser = nullptr;

        parent->addChild(node);

        if (isRoot) {
            m_files[validFileName] = fe;

            m_lastFile = fe;

            VRRegisterSceneGraph::instance()->registerNode(node,
                                                           parent->getName());
        }
    }

    if (isRoot)
    {
        //VRViewer::instance()->forceCompile();
  
		if (node)
            OpenCOVER::instance()->hud->setText2("done loading");
        else
            OpenCOVER::instance()->hud->setText2("failed to load");
        OpenCOVER::instance()->hud->redraw();
    }
    else
    {
        delete fe;
    }

    this->fileFBMap.erase(key);
    return node;
}

osg::Node *coVRFileManager::replaceFile(const char *fileName, coTUIFileBrowserButton *fb, osg::Group *parent, const char *covise_key)
{
    return loadFile(fileName, fb, parent, covise_key);
}

struct Magic
{
    const char *text;
    const char *format;
};

static const int numMagic = 4;
static Magic magic[numMagic] = {
    { "VRML V1.", "iv" },
    { "INVENTOR", "iv" },
    { "#VRML V2", "wrl" },
    { "#X3D V3", "wrl" }
};

std::string coVRFileManager::findFileExt(const Url &url)
{
    if (url.scheme() == "file")
    {
        const char *filename = url.path().c_str();
        FILE *infile = fopen(filename, "r");
        if (infile)
        {

            char inbuffer[128], upcase[128];
            if (fgets(inbuffer, 128, infile) == NULL)
            {
                if (cover->debugLevel(3))
                    std::cerr << "coVRFileManager::findFileExt: fgets failed" << std::endl;
            }

            fclose(infile);

#ifdef VERBOSE
            cerr << " #### filename='" << filename
                 << "'   header='" << inbuffer
                 << "'" << endl;
#endif

            inbuffer[127] = '\0';
            char *iPtr = inbuffer;
            char *oPtr = upcase;

            while (*iPtr)
            {
                if (*iPtr >= 'a' && *iPtr <= 'z')
                    *oPtr = *iPtr - 'a' + 'A';
                else
                    *oPtr = *iPtr;
                iPtr++;
                oPtr++;
            }
            *oPtr = '\0';
            //cerr << upcase << endl;

            for (int i = 0; i < numMagic; i++)
            {
                if (strstr(upcase, magic[i].text))
                {
#ifdef VERBOSE
                    cerr << "Identified file " << filename
                         << " as " << magic[i].format << endl;
#endif
                    return magic[i].format;
                }
            }
        }
    }
    const auto &path = url.path();
    /* look for final "." in filename */
    const char *ext = strchr(path.c_str(), '.');
    /* no dot, assume it's just the extension */
    if (ext == NULL)
        return std::string();
    else
        /* advance "ext" past the period character */
        ++ext;
    // get rid of uppercase/lowercase endings
    while (ext)
    {
        // VRML
        if (0 == strcasecmp(ext, "vrml"))
            return "wrl";
        if (0 == strcasecmp(ext, "wrl"))
            return "wrl";
        if (0 == strcasecmp(ext, "wrl.gz"))
            return "wrl";
        if (0 == strcasecmp(ext, "wrz"))
            return "wrl";

        // X3DV
        if (0 == strcasecmp(ext, "x3dv"))
            return "wrl";
        if (0 == strcasecmp(ext, "x3dv.gz"))
            return "wrl";
        if (0 == strcasecmp(ext, ".x3dvz"))
            return "wrl";

        // volume data
        if (0 == strcasecmp(ext, "avf"))
            return "avf";
        if (0 == strcasecmp(ext, "xvf"))
            return "xvf";
        if (0 == strcasecmp(ext, "rvf"))
            return "rvf";

        // others
        if (0 == strcasecmp(ext, "iv"))
            return "iv";
        if (0 == strcasecmp(ext, "ive"))
            return "ive";
        if (0 == strcasecmp(ext, "osg"))
            return "osg";
        if (0 == strcasecmp(ext, "obj"))
            return "obj";
        if (0 == strcasecmp(ext, "jt"))
            return "jt";

        const char *newext = strchr(ext, '.');
        if (newext == NULL)
            return ext;
        else
            ext = newext + 1;
    }
    return ext;
}

void coVRFileManager::reloadFile()
{
    START("coVRFileManager::reloadFile");
    if (m_lastFile)
    {
        std::cerr << "reloading " << m_lastFile->url << std::endl;
        m_loadingFile = m_lastFile;
        m_lastFile->reload();
        m_loadingFile = nullptr;
    }
    else
    {
        std::cerr << "nothing to reload" << std::endl;
    }
}

void coVRFileManager::unloadFile(const char *file)
{
    START("coVRFileManager::unloadFile");
    if (file)
    {
        //std::cerr << "coVRFileManager::unloadFile: file=" << file << std::endl;
        std::string validFileName(file);
        convertBackslash(validFileName);
        auto it = m_files.find(validFileName);
        if (it == m_files.end())
        {
            std::cerr << validFileName << " not loaded" << std::endl;
            return;
        }

        auto &fe = it->second;
        //VRRegisterSceneGraph::instance()->unregisterNode(fe->node, parent->getName());
        if (!fe->parents.empty())
        {
            fe->setVisible(true); // otherwise invalid parent nodes would stick around
            if (fe->button)
                fe->button->setState(true);
        }

        if (!fe->unload())
            std::cerr << "unloading " << fe->url << " failed" << std::endl;
        fe->updateButton();

        //std::cerr << "unloaded " << fe->url << ": #instances=" << fe->numInstances() << std::endl;

        if (fe->numInstances() == 0)
        {
            if (m_lastFile == fe)
                m_lastFile = nullptr;
            delete fe;
            m_files.erase(it);
        }
    }
    else
    {
        m_lastFile = nullptr;
        for (auto &fe: m_files)
        {
            while (fe.second->unload())
                ;
            fe.second->updateButton();
            delete fe.second;
        }
        m_files.clear();
    }
}

coVRFileManager *coVRFileManager::instance()
{
    if (!s_instance)
        s_instance = new coVRFileManager;
    return s_instance;
}

coVRFileManager::coVRFileManager()
    : fileHandlerList()
    , m_sharedFiles("coVRFileManager_filePaths", fileOwnerList(), vrb::ALWAYS_SHARE)
{
    START("coVRFileManager::coVRFileManager");
    /// path for the viewpoint file: initialized by 1st param() call
    assert(!s_instance);
    getSharedDataPath();
	//register my files with my ID
	coVRCommunication::instance()->addOnConnectCallback([this](void) {
		fileOwnerList files = m_sharedFiles.value();
		auto path = files.begin();
		while (path != files.end())
		{
			path->second = coVRCommunication::instance()->getID();
			++path;
		}
		m_sharedFiles = files;
		});
    if (cover) {
        m_owner.reset(new ui::Owner("FileManager", cover->ui));

        auto fileOpen = new ui::FileBrowser("OpenFile", m_owner.get());
        fileOpen->setText("Open");
        fileOpen->setFilter(getFilterList());
        if(cover->fileMenu)
        {
          cover->fileMenu->add(fileOpen);
          fileOpen->setCallback([this](const std::string &file){
                  loadFile(file.c_str());
          });
          m_sharedFiles.setUpdateFunction([this](void) {loadPartnerFiles(); });
          m_fileGroup = new ui::Group("LoadedFiles", m_owner.get());
          m_fileGroup->setText("Files");
          cover->fileMenu->add(m_fileGroup);
  
          auto fileReload = new ui::Action("ReloadFile", m_owner.get());
          cover->fileMenu->add(fileReload);
          fileReload->setText("Reload file");
          fileReload->setCallback([this](){
                  reloadFile();
          });

          auto fileSave = new ui::FileBrowser("SaveFile", m_owner.get(), true);
          fileSave->setText("Save");
          fileSave->setFilter(getWriteFilterList());
          cover->fileMenu->add(fileSave);
          fileSave->setCallback([this](const std::string &file){
                  if (coVRMSController::instance()->isMaster())
                  VRSceneGraph::instance()->saveScenegraph(file);
          });

          cover->getUpdateManager()->add(this);
		  coVRCommunication::instance()->initVrbFileMenu();
        }
    }

    osgDB::Registry::instance()->addFileExtensionAlias("gml", "citygml");

    options = new osgDB::ReaderWriter::Options;
    options->setOptionString(coCoviseConfig::getEntry("options", "COVER.File"));
    osgDB::Registry::instance()->setOptions(options);

	remoteFetchEnabled = coCoviseConfig::isOn("value", "System.VRB.RemoteFetch", false, nullptr);
	std::string path = coCoviseConfig::getEntry("path", "System.VRB.RemoteFetch");
	path = resolveEnvs(path);
	if (fileExist(path))
	{
		remoteFetchPath = path;
	}
	else
	{
		std::string tmp = fs::temp_directory_path().string() + "/OpenCOVER"; 
        try
        {
            fs::create_directory(tmp);
        }
        catch (...)
        {
            fprintf(stderr, "Could not create directory %s\n", tmp.c_str());
        }
		remoteFetchPath = tmp;
	}
}

coVRFileManager::~coVRFileManager()
{
    START("coVRFileManager::~coVRFileManager");
    if (cover->debugLevel(2))
        fprintf(stderr, "delete coVRFileManager\n");

#if 0
    for (auto &f: m_files)
    {
        delete f.second;
    }
    m_files.clear();
#endif

    cover->getUpdateManager()->remove(this);

    s_instance = NULL;
}

//=====================================================================
//
//=====================================================================
const char *coVRFileManager::getName(const char *file)
{
    START("coVRFileManager::getName");
    static char *buf = NULL;
    static int buflen = 0;

    if (file == NULL)
        return NULL;
    else if (file[0] == '\0')
        return NULL;

    if ((buf == NULL) || (buflen < (int)(strlen(file) + 20)))
    {
        buflen = strlen(file) + 100;
        delete[] buf;
        buf = new char[buflen];
    }
    sprintf(buf, "%s", file);
    FILE *fp = ::fopen(buf, "r");
    if (fp != NULL)
    {
        fclose(fp);
        return buf;
    }

    char *covisepath = getenv("COVISE_PATH");
    if (!covisepath)
    {
        if (cover->debugLevel(3))
            cerr << "ERROR: COVISE_PATH not defined\n";
        return NULL;
    }
    if ((buf == NULL) || (buflen < (int)(strlen(covisepath) + strlen(file) + 20)))
    {
        buflen = strlen(covisepath) + strlen(file) + 100;
        delete[] buf;
        buf = new char[buflen];
    }
    char *coPath = new char[strlen(covisepath) + 1];
    strcpy(coPath, covisepath);
#ifdef _WIN32
    char *dirname = strtok(coPath, ";");
#else
    char *dirname = strtok(coPath, ":");
#endif
    while (dirname != NULL)
    {
        sprintf(buf, "%s/%s", dirname, file);
        boost::filesystem::path p(buf);
        if(boost::filesystem::exists(p))
        {
            delete[] coPath;
            return buf;
        }
#if 0
        for (int i = strlen(dirname) - 2; i > 0; i--)
        {
            if (dirname[i] == '/')
            {
                dirname[i] = '\0';
                break;
            }
            else if (dirname[i] == '\\')
            {
                dirname[i] = '\0';
                break;
            }
        }
        sprintf(buf, "%s/%s", dirname, file);
        fp = ::fopen(buf, "r");
        if (fp != NULL)
        {
            fclose(fp);
            delete[] coPath;
            return buf;
        }
#endif
#ifdef _WIN32
        dirname = strtok(NULL, ";");
#else
        dirname = strtok(NULL, ":");
#endif
    }
    delete[] coPath;
    buf[0] = '\0';
    return NULL;
}

bool coVRFileManager::makeRelativeToSharedDataLink(std::string & fileName)
{
	if (fs::exists(fileName))
	{
		return makeRelativePath(fileName, m_sharedDataLink);
	}
	return false;
	
}
bool coVRFileManager::isInTmpDir(const std::string& fileName)
{
	std::string f(fileName);
	return makeRelativePath(f, fs::temp_directory_path().string());
}
bool coVRFileManager::makeRelativePath(std::string& fileName,  const std::string& basePath)
{
	convertBackslash(fileName);
	std::string bp(basePath);
	convertBackslash(bp);
	if (bp.length() >= fileName.length())
	{
		return false;
	}
	if (int pos = fileName.find(bp) != std::string::npos)
	{
		fileName.erase(0, pos + basePath.length() - 1);
		return true;
	}
	std::string abs = fs::canonical(bp).string();
	convertBackslash(abs);
	for (size_t i = 0; i < abs.length(); i++)
	{
		if (std::tolower(abs[i]) != std::tolower(fileName[i]))
		{
			return false;
		}
	}
	fileName.erase(0, abs.length());
	return true;
}
std::string coVRFileManager::findOrGetFile(const std::string& filePath, bool *isTmp)
{
	coVRMSController* ms = coVRMSController::instance();
	enum FilePlace
	{
		MISS = 0,		//file not found
		LOCAL,		//local file
		WORK,		//in current working directory
		LINK,		//under shared data link
		FETCHED,		//already in remote fetch directory
		REMOTE		//fetch from remote in tmp directory
	};
	FilePlace filePlace = MISS;
	std::string path;
	//find local file
	if (fileExist(filePath))
	{
		path = filePath;
		convertBackslash(path);
		filePlace = LOCAL;
	}
	else if (fileExist(path = fs::current_path().string() + filePath) || fileExist(path = fs::current_path().string() + "/" + filePath))//find file in working dir
	{
		filePlace = WORK;
	}
	else if (fileExist(path = m_sharedDataLink + filePath))//find file under sharedData link
	{

		filePlace = LINK;
	}
	else if (remoteFetchPath.size() != 0 && fileExist(path = remoteFetchPath + "/" + getFileName(filePath)))
	{
		filePlace = FETCHED;
	}
	if (remoteFetchEnabled) //check if all have found the find locally
	{
		bool sync = true;
		bool found = filePlace != MISS;
		if (ms->isMaster())
		{
			coVRMSController::SlaveData sd(sizeof(bool));

			ms->readSlaves(&sd);
			for (size_t i = 0; i < sd.data.size(); i++)
			{
				if (*(bool*)sd.data[i] != found)
				{
					sync = false;
				}
			}
			ms->sendSlaves(&sync, sizeof(sync));
			if (!sync)
			{
				ms->sendSlaves(&found, sizeof(found));
				if (found)
				{
					covise::TokenBuffer tb;
					if (!serializeFile(path, tb))
					{
						cerr << "coVRFileManager::findOrGetFile error 1: file was there and is now gone" << endl;
						exit(1);
					}
					covise::Message msg(tb);
					ms->sendSlaves(&msg);
				}
			}
		}
		else //is slave
		{
			ms->sendMaster(&found, sizeof(found));
			ms->readMaster(&sync, sizeof(sync));
			if (!sync)
			{
				bool master_found;
				ms->readMaster(&master_found, sizeof(master_found));
				if (master_found) //receive file from master
				{
					covise::Message msg;
					ms->readMaster(&msg);
					int numBytes = 0;
					covise::TokenBuffer tb(&msg);
					tb >> numBytes;
					if (numBytes <= 0)
					{
						path = "";
					}
					else
					{
						const char* buf = tb.getBinary(numBytes);
						path = writeFile(getFileName(std::string(filePath)), buf, numBytes);
					}
				}
				else
				{
					filePlace = MISS;
				}
			}
		}
		if (filePlace == MISS)
		{
			path = "";
			//fetch the file
			int fileOwner = guessFileOwner(filePath);
			path = remoteFetch(filePath, fileOwner);
			if (fileExist(path))
			{
				//isTmp = true; //dont ever delete tmp files
				filePlace = REMOTE;
			}
		}
	}
	if (filePlace == MISS)
	{
		path = "";
	}

	return path;
}

std::string coVRFileManager::getFontFile(const char *fontname)
{
    std::string fontFile = "share/covise/fonts/";
    if (fontname)
    {
        fontFile += fontname;
    }
    else
    {
        fontFile += coCoviseConfig::getEntry("value", "COVER.Font", "DroidSansFallbackFull.ttf");
    }
    fontFile = getName(fontFile.c_str());
    return fontFile;
}

osg::ref_ptr<osgText::Font> coVRFileManager::loadFont(const char *fontname)
{
    osg::ref_ptr<osgText::Font> font;
    font = osgText::readRefFontFile(getFontFile(fontname));
    if (font == NULL)
    {
        font = osgText::readRefFontFile(getFontFile(NULL));
    }
    return font;
}

int coVRFileManager::coLoadFontDefaultStyle()
{

    return 0;
}

std::string coVRFileManager::getFilterList()
{
    std::string extensions;
    for (FileHandlerList::iterator it = fileHandlerList.begin();
         it != fileHandlerList.end();
         ++it)
    {
        extensions += "*.";
        extensions += (*it)->extension;
        extensions += ";";
    }
    extensions += "*.osg *.ive;";
    extensions += "*.osgb *.osgt *.osgx;";
    extensions += "*.obj;";
    extensions += "*.stl;";
    extensions += "*.ply;";
    extensions += "*.iv;";
    extensions += "*.dxf;";
    extensions += "*.3ds;";
    extensions += "*.flt;";
    extensions += "*.dae;";
    extensions += "*.md2;";
    extensions += "*.geo;";
    extensions += "*";

    return extensions;
}

std::string coVRFileManager::getWriteFilterList()
{
    std::string extensions;
    extensions += "*.osg;";
    extensions += "*.ive;";
    extensions += "*.osgb;";
    extensions += "*.osgt;";
    extensions += "*.osgx;";
    extensions += "*.obj;";
    extensions += "*.stl;";
    extensions += "*.3ds;";
    extensions += "*.iv;";
    extensions += "*.dae;";
    extensions += "*";

    return extensions;
}


const FileHandler *coVRFileManager::getFileHandler(const char *extension)
{
    for (FileHandlerList::iterator it = fileHandlerList.begin();
         it != fileHandlerList.end();
         ++it)
    {
        if (!strcasecmp(extension, (*it)->extension))
            return *it;
    }

    return NULL;
}

const FileHandler *coVRFileManager::findFileHandler(const char *pathname)
{
    std::vector<const char *> extensions;
    if (const char *p = strrchr(pathname, '/'))
    {
        extensions.push_back(p+1);
    }
    else
    {
        extensions.push_back(pathname);
    }
    for (const char *p = strchr(pathname, '.'); p; p = strchr(p, '.'))
    {
        ++p;
        const char *extension = p;
        extensions.push_back(extension);
    }

    for (auto extension: extensions)
    {
        for (FileHandlerList::iterator it = fileHandlerList.begin();
                it != fileHandlerList.end();
                ++it)
        {
            if (!strcasecmp(extension, (*it)->extension))
                return *it;
        }

        int extlen = strlen(extension);
        char *cEntry = new char[40 + extlen];
        char *lowerExt = new char[extlen + 1];
        for (size_t i = 0; i < extlen; i++)
        {
            lowerExt[i] = tolower(extension[i]);
            if (lowerExt[i] == '.')
                lowerExt[i] = '_';
        }
        lowerExt[extlen] = '\0';

        sprintf(cEntry, "COVER.FileManager.FileType:%s", lowerExt);
        string plugin = coCoviseConfig::getEntry("plugin", cEntry);
        delete[] cEntry;
        delete[] lowerExt;
        if (plugin.size() > 0)
        { // load the appropriate plugin and give it another try
            coVRPluginList::instance()->addPlugin(plugin.c_str());
            for (FileHandlerList::iterator it = fileHandlerList.begin();
                    it != fileHandlerList.end();
                    ++it)
            {
                if (!strcasecmp(extension, (*it)->extension))
                {
                    if (cover->debugLevel(2))
                        fprintf(stderr, "coVRFileManager::findFileHandler(extension=%s), using plugin %s\n", extension, plugin.c_str());
                    return *it;
                }
            }
        }
    }

    return NULL;
}

coVRIOReader *coVRFileManager::findIOHandler(const char *pathname)
{
    coVRIOReader *best = NULL;
    size_t maxmatch = 0;
    const std::string file(pathname);

    for (IOReaderList::iterator it = ioReaderList.begin();
         it != ioReaderList.end(); ++it)
    {
        typedef std::list<std::string> ExtList;
        const ExtList &extlist = (*it)->getSupportedReadFileExtensions();
        for (ExtList::const_iterator it2 = extlist.begin();
                it2 != extlist.end();
                ++it2)
        {
            const std::string &ext = *it2;
            if (ext.length() > file.length())
                continue;
            if (std::equal(ext.rbegin(), ext.rend(), file.rbegin()))
            {
                if (ext.length() > maxmatch)
                {
                    maxmatch = ext.length();
                    best = *it;
                }
            }

        }
    }
    return best;
}

int coVRFileManager::registerFileHandler(const FileHandler *handler)
{
    if (getFileHandler(handler->extension))
        return -1;

    fileHandlerList.push_back(handler);
    return 0;
}

int coVRFileManager::registerFileHandler(coVRIOReader *handler)
{
    //if(getFileHandler(handler->extension))
    //   return -1;

    if (cover->debugLevel(3))
        std::cerr << "coVRFileManager::registerFileHandler info: registering " << handler->getIOHandlerName() << std::endl;

    this->ioReaderList.push_back(handler);
    return 0;
}

int coVRFileManager::unregisterFileHandler(const FileHandler *handler)
{
    if (!handler || !handler->extension)
        return -1;

    for (FileHandlerList::iterator it = fileHandlerList.begin();
         it != fileHandlerList.end();
         ++it)
    {
        if (strcmp(handler->extension, (*it)->extension) == 0)
        {
            const FileHandler *p = *it;
            if (p->loadFile == handler->loadFile
                && p->unloadFile == handler->unloadFile)
            {
                for (auto it = m_files.begin(), next = it; it != m_files.end(); it = next)
                {
                    auto &fe = it->second;
                    if (m_lastFile == fe)
                        m_lastFile = nullptr;
                    if (fe->handler == p)
                    {
                        while (fe->unload())
                            ;
                        fe->updateButton();
                        delete fe;
                        next = m_files.erase(it);
                    }
                    next = it;
                    ++next;
                }

                fileHandlerList.erase(it);
                return 0;
            }
        }
    }
    return -1;
}

int coVRFileManager::unregisterFileHandler(coVRIOReader *handler)
{
    if (handler == 0)
        return -1;

    if (cover->debugLevel(3))
        std::cerr << "coVRFileManager::unregisterFileHandler info: unregistering " << handler->getIOHandlerName() << std::endl;

    for (IOReaderList::iterator it = ioReaderList.begin();
         it != ioReaderList.end(); ++it)
    {
        if (*it == handler)
        {
            ioReaderList.erase(it);
            return 0;
        }
    }
    return -1;
}

coTUIFileBrowserButton *coVRFileManager::getMatchingFileBrowserInstance(std::string keyFileName)
{
    coTUIFileBrowserButton *fbo = NULL;
    std::map<string, coTUIFileBrowserButton *>::iterator itr = this->fileFBMap.find(keyFileName);

    if (itr != this->fileFBMap.end())
        fbo = itr->second;
    else
    {
        if (this->IsDefFBSet())
            fbo = this->mDefaultFB;
        else
            fbo = NULL;
    }

    return fbo;
}

bool coVRFileManager::IsDefFBSet()
{
    if (this->mDefaultFB)
        return true;
    else
        return false;
}

void coVRFileManager::SetDefaultFB(coTUIFileBrowserButton *fb)
{
    this->mDefaultFB = fb;
}

void coVRFileManager::setViewPointFile(const std::string &file)
{
    viewPointFile = file;
}

std::string coVRFileManager::getViewPointFile()
{
    return viewPointFile;
}

bool coVRFileManager::update()
{
    for (ReadOperations::iterator op = this->readOperations.begin(); op != this->readOperations.end(); ++op)
    {
        std::string reader = op->first;
        if (this->readOperations[reader].empty())
        {
            this->readOperations.erase(op);
            continue;
        }

        IOReadOperation readOperation = this->readOperations[reader].front();

        coVRIOReader::IOStatus status = readOperation.reader->loadPart(readOperation.filename, readOperation.group);

        if (status == coVRIOReader::Finished)
        {
            if (cover->debugLevel(3))
                std::cerr << "coVRFileManager::update info: finished loading " << readOperation.filename << std::endl;
            readOperations[reader].pop_front();
        }
        else if (status == coVRIOReader::Failed)
        {
            if (cover->debugLevel(3))
                std::cerr << "coVRFileManager::update info: failed loading " << readOperation.filename << std::endl;
            readOperations[reader].pop_front();
        }
        else
        {
            if (cover->debugLevel(3))
                std::cerr << "coVRFileManager::update info: loading " << readOperation.filename << " (" << readOperation.reader->getIOProgress() << ")" << std::endl;
        }
    }
    return true;
}

void coVRFileManager::loadPartnerFiles()
{
    //load and unload existing files
    std::set<std::string> alreadyLoadedFiles;
    for (auto myFile : m_files)
    {
        auto shortPath = myFile.first;
		auto fileName = getFileName(shortPath);
        makeRelativeToSharedDataLink(shortPath);
        alreadyLoadedFiles.insert(fileName);
		bool found = false;
		for (auto p : m_sharedFiles.value())
		{
			auto pFileName = getFileName(p.first);
			if (pFileName == fileName)
			{
				//myFile.second->load();
				found = true;
				break;
			}
		}
		if (!found)
		{
			//unloadFile(myFile.first.c_str());
		}
    }

    //load new partner files
    std::set<std::string> newFiles;
	for (auto theirFile : m_sharedFiles.value())
	{
		if (alreadyLoadedFiles.find(getFileName(theirFile.first)) == alreadyLoadedFiles.end())
		{
			loadFile(theirFile.first.c_str());
		}
	}
}

void coVRFileManager::getSharedDataPath()
{
    char *covisepath = getenv("COVISE_PATH");
#ifdef WIN32
    std::vector<std::string> p = split(covisepath, ';');
#else
    std::vector<std::string> p = split(covisepath, ':');
#endif
    for (auto path : p)
    {
		convertBackslash(path);
		int delimiter = path.rfind('/');
		std::string link = path.erase(delimiter, path.length()- delimiter) + "/sharedData";
        if (fileExist(link))
        {
			m_sharedDataLink = link;
			return;
        }
    }
}

void coVRFileManager::convertBackslash(std::string & path)
{
    std::string convertedPath;
    for (char c : path)
    {
        if (c == '\\')
        {
            convertedPath.push_back('/');
        }
        else
        {
            convertedPath.push_back(c);
        }
    }
    path = convertedPath;
}

std::string coVRFileManager::remoteFetch(const std::string& filePath, int fileOwner)
{

	const char *buf = nullptr;
    int numBytes = 0;
	if (OpenCOVER::instance()->isVRBconnected() ||!coVRMSController::instance()->isMaster())
	{
		if (coVRMSController::instance()->isMaster())
		{
			TokenBuffer rtb;
			rtb << filePath;
			rtb << coVRCommunication::instance()->getID();
			rtb << fileOwner;
			Message m(rtb);
			m.type = COVISE_MESSAGE_VRB_REQUEST_FILE;
			cover->sendVrbMessage(&m);
		}
		int message = 1;
		Message* msg= new Message; 
		Message* mymsg = nullptr;
		//wait for the file
		do
		{
			if (OpenCOVER::instance()->isVRBconnected() || !coVRMSController::instance()->isMaster())
			{
				if (coVRMSController::instance()->isMaster())
				{
					OpenCOVER::instance()->vrbc()->wait(msg);
					coVRMSController::instance()->sendSlaves((char*)& message, sizeof(message));
					coVRMSController::instance()->sendSlaves(msg);
				}
				else
				{
					coVRMSController::instance()->readMaster((char*)& message, sizeof(message));
					if (message == 0)
						return "";
					// wait for message from master instead
					coVRMSController::instance()->readMaster(msg);
				}
				//cache all send file messages
				if (msg->type == COVISE_MESSAGE_VRB_SEND_FILE)
				{
					m_sendFileMessages.push_back(msg);
				}
				else
				{
					coVRCommunication::instance()->handleVRB(*msg);
				}
			}
			else
			{
					message = 0;
					coVRMSController::instance()->sendSlaves((char*)& message, sizeof(message));

				return "";
			}
			int myID;
			std::string fn;
			auto m = m_sendFileMessages.begin();
			//find out if my file has been received
			while (m != m_sendFileMessages.end())
			{
				TokenBuffer tb(*m);
				tb.rewind();
				tb >> myID;
				tb >> fn;
				if (fn == std::string(filePath) && myID == coVRCommunication::instance()->getID())
				{
					mymsg = *m;
					m_sendFileMessages.erase(m);
					m = m_sendFileMessages.end();
				}
				else
				{
					++m;
				}
			}
		} while (!mymsg);

		
		TokenBuffer tb(mymsg);
		int myID;
		std::string fn;
		tb >> myID; // this should be my ID
		tb >> fn; //this should be the requested file
		tb >> numBytes;
		if (numBytes <= 0)
		{
			return "";
		}
		buf = tb.getBinary(numBytes);
		std::string pathToTmpFile = writeFile(getFileName(std::string(filePath)), buf, numBytes);
		delete mymsg;
		mymsg = nullptr;
		return pathToTmpFile;
	}

	return "";
}
int coVRFileManager::getFileId(const char* url)
{
	if (!url)
	{
		return -1;
	}
	std::string p(url);
	makeRelativeToSharedDataLink(p);
	for (size_t i = 0; i < m_sharedFiles.value().size(); i++)
	{
		if (m_sharedFiles.value()[i].first == p)
		{
			return i;
		}
	}
	return -1;
}
void coVRFileManager::sendFile(TokenBuffer &tb)
{
    const char *filename;
    tb >> filename;
    int requestorsID;
    tb >> requestorsID;
    std::string validPath(filename);
    //if filenot found
    if (!fileExist(validPath))
    {
        //search file under sharedDataPath
        makeRelativeToSharedDataLink(validPath);
        validPath = m_sharedDataLink + validPath;
    }
	TokenBuffer rtb;
	rtb << requestorsID;
	rtb << filename;

	serializeFile(validPath, rtb);
	coVRCommunication::instance()->send(rtb, COVISE_MESSAGE_VRB_SEND_FILE);

}
std::string coVRFileManager::getFileName(const std::string &fileName)
{
    std::string name;
    for (size_t i = fileName.length() - 1; i > 0; --i)
    {
        if (fileName[i] == '/' || fileName[i] == '\\')
        {
            return name;
        }
        name.insert(name.begin(), fileName[i]);
    }
    return fileName;
}
std::string coVRFileManager::cutFileName(const std::string &fileName)
{
    std::string name = fileName;
    for (size_t i = fileName.length() - 1; i > 0; --i)
    {
        name.pop_back();
        if (fileName[i] == '/' || fileName[i] == '\\')
        {
            return name;
        }

    }
    return "";
}
std::string coVRFileManager::reduceToAlphanumeric(const std::string &str)
{

    std::string red;
    for (auto c : str)
    {
        if (isalnum(c) && c != '_' && c != '-')
        {
			red.push_back(c);
        }
    }
    return red;
}

std::string coVRFileManager::writeFile(const std::string& fileName, const char* content, int size)
{
	
	std::string p(remoteFetchPath);
	p += "/" + fileName;

	if ((size > 0) && !fileExist(p))
	{
#ifndef _WIN32
		int fd = open(p.c_str(), O_RDWR | O_CREAT, 0777);
#else
		int fd = open(p.c_str(), O_RDWR | O_CREAT | O_BINARY, 0777);
#endif
		if (fd != -1)
		{
			if (int wroteBytes = write(fd, content, size) != size)
			{
				//warn("remoteFetch: temp file write error\n");
				cerr << fileName << " writing file error: " << "wrote bytes = " << wroteBytes << " received bytes = " << size << endl;
				return "";
			}
			close(fd);
		}
		else
		{
			cerr << "opening file " << p << " failed";
			return "";
		}
	}
	return p;
}
int coVRFileManager::guessFileOwner(const std::string& fileName)
{

	int fileOwner = -1;
	int bestmatch = 0;
	for (auto p : m_sharedFiles.value())
	{
		int match = 0;
		for (size_t i = 0; i < std::min(p.first.length(), fileName.length()); i++)
		{
			if (p.first[i] == fileName[i])
			{
				++match;
			}
		}
		if (match > bestmatch)
		{
			bestmatch = match;
			fileOwner = p.second;
		}
	}
	return fileOwner;
}
bool coVRFileManager::serializeFile(const std::string& fileName, covise::TokenBuffer& tb)
{
	struct stat statbuf;


	if (stat(fileName.c_str(), &statbuf) < 0)
	{
		tb << 0;
		return false;
	}
#ifdef _WIN32
	int fdesc = open(fileName.c_str(), O_RDONLY | O_BINARY);
#else
	int fdesc = open(fileName.c_str(), O_RDONLY);
#endif
	if (fdesc > 0)
	{
		tb << (int)statbuf.st_size;
		char* buf = (char*)tb.allocBinary(statbuf.st_size);
		int n = read(fdesc, buf, statbuf.st_size);
		if (n == -1)
		{
			cerr << "coVRCommunication::handleVRB: read failed: " << strerror(errno) << endl;
		}
		close(fdesc);
	}
	else
	{
		cerr << " file access error, could not open " << fileName << endl;
		tb << 0;
		return false;
	}
	return true;
}
std::string coVRFileManager::cutStringAt(const std::string &s, char delimiter)
{
	std::string r;
	auto it = s.begin();
	while (it != s.end() && *it != delimiter)
	{
		r.push_back(*it);
		++it;
	}
	return r;
}

std::string coVRFileManager::resolveEnvs(const std::string& s)
{
	std::string stringWithoutEnvs;
#ifdef WIN32	
	char delimiter = '%';

#else 
	char delimiter = '$';
	char delimiter2 = '/';
#endif
	auto it = s.begin();
	auto lastIt = s.begin();
	while (it != s.end())
	{
		if (*it == delimiter)
		{
			stringWithoutEnvs.append(lastIt, it);
			++it;
			std::string env;
#ifdef WIN32
			while (*it != delimiter)
#else		
			while(*it != delimiter2)
#endif
			{

				env.push_back(*it);
				++it;
			}

			env = getenv(env.c_str());
			env = cutStringAt(env, ';');
#ifndef WIN32
			env.push_back(delimiter2);
#endif // !WIN32
			stringWithoutEnvs += env;
			++it;
			lastIt = it;
		}
		++it;
	}
	stringWithoutEnvs.append(lastIt, s.end());
	return stringWithoutEnvs;
}

}
