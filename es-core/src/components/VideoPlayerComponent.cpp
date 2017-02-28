#ifdef _RPI_
#include "components/VideoPlayerComponent.h"
#include "Renderer.h"
#include "ThemeData.h"
#include "Util.h"
#include <signal.h>
#include <wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

std::string getTitlePath() {
	std::string home = getHomePath();
	return home + "/.emulationstation/last_title.txt";
}

VideoPlayerComponent::VideoPlayerComponent(Window* window, bool useSubtitles) :
	VideoComponent(window),
	mPlayerPid(-1),
	subtitles(useSubtitles)
{
}

VideoPlayerComponent::~VideoPlayerComponent()
{
	stopVideo();
}

void VideoPlayerComponent::render(const Eigen::Affine3f& parentTrans)
{
	VideoComponent::render(parentTrans);
}

void VideoPlayerComponent::startVideo()
{
	if (!mIsPlaying) 
	{
		mVideoWidth = 0;
		mVideoHeight = 0;

		std::string path(mVideoPath.c_str());

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
				// Update the playing state
				mIsPlaying = true;
				mFadeIn = 0.0f;
			}
			else
			{
				// Find out the pixel position of the video view and build a command line for
				// omxplayer to position it in the right place
				char buf[32];
				float x = mPosition.x() - (mOrigin.x() * mSize.x());
				float y = mPosition.y() - (mOrigin.y() * mSize.y());
				sprintf(buf, "%d,%d,%d,%d", (int)x, (int)y, (int)(x + mSize.x()), (int)(y + mSize.y()));
				// We need to specify the layer of 10000 or above to ensure the video is displayed on top
				// of our SDL display
				const char* argv[] = { "", "--layer", "10010", "--loop", "--no-osd", "--aspect-mode", "letterbox", "--win", buf, "-b", "", "", "", "", "","", NULL };
				
				if (subtitles) 
				{	
					argv[7] = "--subtitles";
					argv[8] = getTitlePath().c_str();
					argv[9] = mPlayingVideoPath.c_str();
					/*argv[10] = "--no-ghost-box";
					argv[11] = "--align"; 
					argv[12] = "center";*/
					argv[15] = "";
					
				} 
				else 
				{
					argv[9] = mPlayingVideoPath.c_str();
				}
				
				//const char* argv[] = args;
				const char* env[] = { "LD_LIBRARY_PATH=/opt/vc/libs:/usr/lib/omxplayer", NULL };
				// Fill in the empty argument with the video path
				//argv[10] = mPlayingVideoPath.c_str();
				
				// Redirect stdout
				int fdin = open("/dev/null", O_RDONLY);
				int fdout = open("/dev/null", O_WRONLY);
				dup2(fdin, 0);
				dup2(fdout, 1);
				// Run the omxplayer binary
				execve("/usr/bin/omxplayer.bin", (char**)argv, (char**)env);
				_exit(EXIT_FAILURE);
			}
		}
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

#endif

