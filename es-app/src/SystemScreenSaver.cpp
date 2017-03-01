#include "SystemScreenSaver.h"
#ifdef _RPI_
#include "components/VideoPlayerComponent.h"
#endif
#include "components/VideoVlcComponent.h"
#include "platform.h"
#include "Renderer.h"
#include "Settings.h"
#include "SystemData.h"
#include "Util.h"
#include "Log.h"
#include <stdio.h>

#define FADE_TIME 			3000
#define SWAP_VIDEO_TIMEOUT	35000

SystemScreenSaver::SystemScreenSaver(Window* window) :
	mVideoScreensaver(NULL),
	mWindow(window),
	mCounted(false),
	mVideoCount(0),
	mState(STATE_INACTIVE),
	mOpacity(0.0f),
	mTimer(0),
	mSystemName(NULL),
	mGameName(NULL),
	mGameIndex(-1)
{
	mWindow->setScreenSaver(this);
}

SystemScreenSaver::~SystemScreenSaver()
{
	// Delete subtitle file, if existing
	remove(getTitlePath().c_str());
	delete mVideoScreensaver;
}

bool SystemScreenSaver::allowSleep()
{
	return false;
}

void SystemScreenSaver::startScreenSaver()
{
	if (!mVideoScreensaver && (Settings::getInstance()->getString("ScreenSaverBehavior") == "random video"))
	{
		// Configure to fade out the windows
		mState = STATE_FADE_OUT_WINDOW;
		mOpacity = 0.0f;

		// Load a random video
		std::string path;
		pickRandomVideo(path);
		LOG(LogDebug) << "Starting Video at path \"" << path << "\"";
		if (!path.empty())
		{
		// Create the correct type of video component
#ifdef _RPI_
			if (Settings::getInstance()->getBool("VideoOmxPlayer"))
				mVideoScreensaver = new VideoPlayerComponent(mWindow, true);
			else
				mVideoScreensaver = new VideoVlcComponent(mWindow);
#else
			mVideoScreensaver = new VideoVlcComponent(mWindow);
#endif


			mVideoScreensaver->setOrigin(0.0f, 0.0f);
			mVideoScreensaver->setPosition(0.0f, 0.0f);
			mVideoScreensaver->setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
			mVideoScreensaver->setVideo(path);
			mVideoScreensaver->onShow();
			mTimer = 0;
		}
		else
		{
			LOG(LogError) << "Path is empty! Path: \"" << path << "\"";
			// No videos. Just use a standard screensaver
			mState = STATE_SCREENSAVER_ACTIVE;
		}
	}
}

void SystemScreenSaver::stopScreenSaver()
{
	delete mVideoScreensaver;
	mVideoScreensaver = NULL;
	mState = STATE_INACTIVE;
}

void SystemScreenSaver::renderScreenSaver()
{
	float lOpacity = mOpacity;
	if (Settings::getInstance()->getBool("VideoOmxPlayer"))
		lOpacity = 1.0f;
	if (mVideoScreensaver)
	{
		// Only render the video if the state requires it
		if ((int)mState >= STATE_FADE_IN_VIDEO)
		{
			Eigen::Affine3f transform = Eigen::Affine3f::Identity();
			mVideoScreensaver->render(transform);
		}
		// Handle any fade
		Renderer::setMatrix(Eigen::Affine3f::Identity());
		Renderer::drawRect(0, 0, Renderer::getScreenWidth(), Renderer::getScreenHeight(), (unsigned char)(lOpacity * 255));
	}
	else if (mState != STATE_INACTIVE)
	{
		Renderer::setMatrix(Eigen::Affine3f::Identity());
		unsigned char opacity = Settings::getInstance()->getString("ScreenSaverBehavior") == "dim" ? 0xA0 : 0xFF;
		Renderer::drawRect(0, 0, Renderer::getScreenWidth(), Renderer::getScreenHeight(), 0x00000000 | opacity);
	}
}

void SystemScreenSaver::countVideos()
{
	if (!mCounted)
	{
		mVideoCount = 0;
		mCounted = true;
		std::vector<SystemData*>:: iterator it;
		for (it = SystemData::sSystemVector.begin(); it != SystemData::sSystemVector.end(); ++it)
		{
			pugi::xml_document doc;
			pugi::xml_node root;
			std::string xmlReadPath = (*it)->getGamelistPath(false);

			if(boost::filesystem::exists(xmlReadPath))
			{
				pugi::xml_parse_result result = doc.load_file(xmlReadPath.c_str());
				if (!result)
					continue;
				root = doc.child("gameList");
				if (!root)
					continue;
				for(pugi::xml_node fileNode = root.child("game"); fileNode; fileNode = fileNode.next_sibling("game"))
				{
					pugi::xml_node videoNode = fileNode.child("video");
					if (videoNode)
						++mVideoCount;
				}
			}
		}
	}
}

void SystemScreenSaver::pickRandomVideo(std::string& path)
{
	countVideos();
	if (mVideoCount > 0)
	{
		srand((unsigned int)time(NULL));
		int video = (int)(((float)rand() / float(RAND_MAX)) * (float)mVideoCount);

		std::vector<SystemData*>:: iterator it;
		for (it = SystemData::sSystemVector.begin(); it != SystemData::sSystemVector.end(); ++it)
		{
			pugi::xml_document doc;
			pugi::xml_node root;
			std::string xmlReadPath = (*it)->getGamelistPath(false);
			int gameIndex = 0;

			if(boost::filesystem::exists(xmlReadPath))
			{
				pugi::xml_parse_result result = doc.load_file(xmlReadPath.c_str());
				if (!result)
					continue;
				root = doc.child("gameList");
				if (!root)
					continue;
				for(pugi::xml_node fileNode = root.child("game"); fileNode; fileNode = fileNode.next_sibling("game"))
				{
					pugi::xml_node videoNode = fileNode.child("video");
					if (videoNode)
					{
						// See if this is the randomly selected video
						if (video-- == 0)
						{
							// Yes. Resolve to a full path
							path = resolvePath(videoNode.text().get(), (*it)->getStartPath(), true).generic_string();	
							mSystemName = (*it)->getFullName().c_str();
							mGameName = fileNode.child("name").text().get();
							mGameIndex = gameIndex;
							writeSubtitle();
							return;
						}
					}
					gameIndex++;
				}
			}
		}
	}
}

void SystemScreenSaver::update(int deltaTime)
{
	// Use this to update the fade value for the current fade stage
	if (mState == STATE_FADE_OUT_WINDOW)
	{
		mOpacity += (float)deltaTime / FADE_TIME;
		if (mOpacity >= 1.0f)
		{
			mOpacity = 1.0f;

			// Update to the next state
			mState = STATE_FADE_IN_VIDEO;
		}
	}
	else if (mState == STATE_FADE_IN_VIDEO)
	{
		mOpacity -= (float)deltaTime / FADE_TIME;
		if (mOpacity <= 0.0f)
		{
			mOpacity = 0.0f;
			// Update to the next state
			mState = STATE_SCREENSAVER_ACTIVE;
		}
	}
	else if (mState == STATE_SCREENSAVER_ACTIVE)
	{
		// Update the timer that swaps the videos
		mTimer += deltaTime;
		if (mTimer > SWAP_VIDEO_TIMEOUT)
		{
			stopScreenSaver();
			startScreenSaver();
			mState = STATE_SCREENSAVER_ACTIVE;
		}
	}

	// If we have a loaded video then update it
	if (mVideoScreensaver)
		mVideoScreensaver->update(deltaTime);
}

const char* SystemScreenSaver::getSystemName()
{
	if (mSystemName)
		return mSystemName;
	else
		return "";
}

const char* SystemScreenSaver::getGameName() 
{
	if (mGameName)
		return mGameName;
	else
		return "";
}

int SystemScreenSaver::getGameIndex()
{
	return mGameIndex;
}

void SystemScreenSaver::writeSubtitle() 
{
	FILE* file = fopen(getTitlePath().c_str(), "w");	
	fprintf(file, "1\n00:00:01,000 --> 00:00:08,000\n");
	fprintf(file, "%s\n", getGameName());
	fprintf(file, "<i>%s</i>\n\n", getSystemName());
	fprintf(file, "2\n00:00:29,000 --> 00:00:35,000\n");
	fprintf(file, "%s\n", getGameName());
	fprintf(file, "<i>%s</i>\n", getSystemName());
	fflush(file);
	fclose(file);
	file = NULL;
}

/*void SystemScreenSaver::input(InputConfig* config, Input input)
{
	LOG(LogDebug) << "Detected input while screensaver: " <<  input.string() << " Game Index: " << getGameIndex();
	if(getGameIndex() >= 0 && (config->isMappedTo("right", input) || config->isMappedTo("start", input))) 
	{
		LOG(LogDebug) << "Detected right input while video screensaver";
		if(config->isMappedTo("right", input)) 
		{
			LOG(LogDebug) << "Next video!";
			// handle screensaver control, first stab
			cancelScreenSaver();
			startScreenSaver();
		}
		else if(config->isMappedTo("start", input)) 
		{
			// launch game!
			LOG(LogDebug) << "Launch Game: " << getGameName() << " - System: " << getSystemName();

			// get game info
			

			// wake up
			mTimeSinceLastInput = 0;
			cancelScreenSaver();
			mSleeping = false;
			onWake();
		}
	}
}
*/