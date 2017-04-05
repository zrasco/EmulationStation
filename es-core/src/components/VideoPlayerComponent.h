#ifdef _RPI_
#ifndef _VIDEOPLAYERCOMPONENT_H_
#define _VIDEOPLAYERCOMPONENT_H_

#include "platform.h"
#include GLHEADER

#include "components/VideoComponent.h"

void catch_child(int sig_num);

class VideoPlayerComponent : public VideoComponent
{
public:
	VideoPlayerComponent(Window* window, std::string path);
	virtual ~VideoPlayerComponent();

	void render(const Eigen::Affine3f& parentTrans) override;

private:
	// Start the video Immediately
	virtual void startVideo();
	// Stop the video
	virtual void stopVideo();

private:
	pid_t							mPlayerPid;
	std::string						subtitlePath;
};

#endif
#endif

