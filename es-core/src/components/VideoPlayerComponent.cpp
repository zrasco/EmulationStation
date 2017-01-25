#include "components/VideoPlayerComponent.h"
#include "Renderer.h"
#include "ThemeData.h"
#include "Util.h"
#include <signal.h>
#include <wait.h>
#ifdef WIN32
#include <codecvt>
#endif

VideoPlayerComponent::VideoPlayerComponent(Window* window) :
	GuiComponent(window),
	mStaticImage(window),
	mVideoHeight(0),
	mVideoWidth(0),
	mStartDelayed(false),
	mIsPlaying(false),
	mShowing(false),
	mPlayerPid(-1)
{
	// Setup the default configuration
	mConfig.showSnapshotDelay 		= false;
	mConfig.showSnapshotNoVideo		= false;
	mConfig.startDelay				= 0;
}

VideoPlayerComponent::~VideoPlayerComponent()
{
	// Stop any currently running video
	stopVideo();
}

void VideoPlayerComponent::setOrigin(float originX, float originY)
{
	mOrigin << originX, originY;

	// Update the embeded static image
	mStaticImage.setOrigin(originX, originY);
}

Eigen::Vector2f VideoPlayerComponent::getCenter() const
{
	return Eigen::Vector2f(mPosition.x() - (getSize().x() * mOrigin.x()) + getSize().x() / 2,
		mPosition.y() - (getSize().y() * mOrigin.y()) + getSize().y() / 2);
}

void VideoPlayerComponent::onSizeChanged()
{
	// Update the embeded static image
	mStaticImage.onSizeChanged();
}

bool VideoPlayerComponent::setVideo(std::string path)
{
	// Get the full native path
	boost::filesystem::path fullPath = getCanonicalPath(path);
	fullPath.make_preferred().native();

	// Check that it's changed
	if (fullPath == mVideoPath)
		return !path.empty();

	// Store the path
	mVideoPath = fullPath;

	// If the file exists then set the new video
	if (!fullPath.empty() && ResourceManager::getInstance()->fileExists(fullPath.generic_string()))
	{
		// Return true to show that we are going to attempt to play a video
		return true;
	}
	// Return false to show that no video will be displayed
	return false;
}

void VideoPlayerComponent::setImage(std::string path)
{
	// Check that the image has changed
	if (path == mStaticImagePath)
		return;
	
	mStaticImage.setImage(path);
	// Make the image stretch to fill the video region
	mStaticImage.setSize(getSize());
	mStaticImagePath = path;
}

void VideoPlayerComponent::setDefaultVideo()
{
	setVideo(mConfig.defaultVideoPath);
}

void VideoPlayerComponent::setOpacity(unsigned char opacity)
{
	// Update the embeded static image
	mStaticImage.setOpacity(opacity);
}

void VideoPlayerComponent::render(const Eigen::Affine3f& parentTrans)
{
}

void VideoPlayerComponent::applyTheme(const std::shared_ptr<ThemeData>& theme, const std::string& view, const std::string& element, unsigned int properties)
{
	using namespace ThemeFlags;

	const ThemeData::ThemeElement* elem = theme->getElement(view, element, "video");
	if(!elem)
	{
		return;
	}

	Eigen::Vector2f scale = getParent() ? getParent()->getSize() : Eigen::Vector2f((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());

	if ((properties & POSITION) && elem->has("pos"))
	{
		Eigen::Vector2f denormalized = elem->get<Eigen::Vector2f>("pos").cwiseProduct(scale);
		setPosition(Eigen::Vector3f(denormalized.x(), denormalized.y(), 0));
	}

	if ((properties & ThemeFlags::SIZE) && elem->has("size"))
	{
		setSize(elem->get<Eigen::Vector2f>("size").cwiseProduct(scale));
	}

	// position + size also implies origin
	if (((properties & ORIGIN) || ((properties & POSITION) && (properties & ThemeFlags::SIZE))) && elem->has("origin"))
		setOrigin(elem->get<Eigen::Vector2f>("origin"));

	if(elem->has("default"))
		mConfig.defaultVideoPath = elem->get<std::string>("default");

	if((properties & ThemeFlags::DELAY) && elem->has("delay"))
		mConfig.startDelay = (unsigned)(elem->get<float>("delay") * 1000.0f);

	if (elem->has("showSnapshotNoVideo"))
		mConfig.showSnapshotNoVideo = elem->get<bool>("showSnapshotNoVideo");

	if (elem->has("showSnapshotDelay"))
		mConfig.showSnapshotDelay = elem->get<bool>("showSnapshotDelay");

	// Update the embeded static image
	mStaticImage.setPosition(getPosition());
	mStaticImage.setMaxSize(getSize());
	mStaticImage.setSize(getSize());
}

std::vector<HelpPrompt> VideoPlayerComponent::getHelpPrompts()
{
	std::vector<HelpPrompt> ret;
	ret.push_back(HelpPrompt("a", "select"));
	return ret;
}

void VideoPlayerComponent::handleStartDelay()
{
	// Only play if any delay has timed out
	if (mStartDelayed)
	{
		if (mStartTime > SDL_GetTicks())
		{
			// Timeout not yet completed
			return;
		}
		// Completed
		mStartDelayed = false;
		// Clear the playing flag so startVideo works
		mIsPlaying = false;
		startVideo();
	}
}

void VideoPlayerComponent::startVideo()
{
	if (!mIsPlaying) {
		mVideoWidth = 0;
		mVideoHeight = 0;

#ifdef WIN32
		std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> wton;
		std::string path = wton.to_bytes(mVideoPath.c_str());
#else
		std::string path(mVideoPath.c_str());
#endif
		// Make sure we have a video path
		if ((path.size() > 0) && (mPlayerPid == -1))
		{
			// Set the video that we are going to be playing so we don't attempt to restart it
			mPlayingVideoPath = mVideoPath;

			// Start the player process
			pid_t pid = fork();
			if (pid == -1)
			{
				// Failed
				mPlayingVideoPath = "";
			}
			else if (pid > 0)
			{
				mPlayerPid = pid;
			}
			else
			{
				float x = mPosition.x() - (mOrigin.x() * mSize.x());
				float y = mPosition.y() - (mOrigin.y() * mSize.y());
				char buf[512];
				sprintf(buf, "%d,%d,%d,%d", (int)x, (int)y, (int)(x + mSize.x()), (int)(y + mSize.y()));

				const char* argv[] = { "", "--win", buf, "--layer", "10000", "--loop", "--no-osd", "", NULL };
				//const char* argv[] = { "", "-noborder", "-ao", "null", "-vo", "xv", "-display", ":0", "-geometry", "320x200+700+500", "/home/roy/tmp/MameVideosMkv/720.mkv", NULL };
				const char* env[] = { "LD_LIBRARY_PATH=/opt/vc/libs:/usr/lib/omxplayer", NULL };
				argv[7] = mPlayingVideoPath.c_str();
				execve("/usr/bin/omxplayer.bin", (char**)argv, (char**)env);
				//execve("/usr/bin/mplayer", (char**)argv, (char**)env);
				_exit(EXIT_FAILURE);
			}
		}
	}
}

void VideoPlayerComponent::startVideoWithDelay()
{
	// If not playing then either start the video or initiate the delay
	if (!mIsPlaying)
	{
		// Set the video that we are going to be playing so we don't attempt to restart it
		mPlayingVideoPath = mVideoPath;

		if (mConfig.startDelay == 0)
		{
			// No delay. Just start the video
			mStartDelayed = false;
			startVideo();
		}
		else
		{
			// Configure the start delay
			mStartDelayed = true;
			mStartTime = SDL_GetTicks() + mConfig.startDelay;
		}
		mIsPlaying = true;
	}
}

void VideoPlayerComponent::stopVideo()
{
	mIsPlaying = false;
	mStartDelayed = false;

	// Stop the player process
	if (mPlayerPid != -1)
	{
		int status;
		kill(mPlayerPid, SIGKILL);
		waitpid(mPlayerPid, &status, WNOHANG);
		mPlayerPid = -1;
	}
}

void VideoPlayerComponent::update(int deltaTime)
{
	manageState();
	handleStartDelay();
	GuiComponent::update(deltaTime);
}

void VideoPlayerComponent::manageState()
{
	// We will only show if the component is on display
	bool show = mShowing;

	// See if we're already playing
	if (mIsPlaying)
	{
		// If we are not on display then stop the video from playing
		if (!show)
		{
			stopVideo();
		}
		else
		{
			if (mVideoPath != mPlayingVideoPath)
			{
				// Path changed. Stop the video. We will start it again below because
				// mIsPlaying will be modified by stopVideo to be false
				stopVideo();
			}
		}
	}
	// Need to recheck variable rather than 'else' because it may be modified above
	if (!mIsPlaying)
	{
		// If we are on display then see if we should start the video
		if (show && !mVideoPath.empty())
		{
			startVideoWithDelay();
		}
	}
}

void VideoPlayerComponent::onShow()
{
	mShowing = true;
	manageState();
}

void VideoPlayerComponent::onHide()
{
	mShowing = false;
	manageState();
}

