#ifndef _VIDEOPLAYERCOMPONENT_H_
#define _VIDEOPLAYERCOMPONENT_H_

#include "platform.h"
#include GLHEADER

#include "GuiComponent.h"
#include "ImageComponent.h"
#include <string>
#include <memory>
#include <boost/filesystem.hpp>

class VideoPlayerComponent : public GuiComponent
{
	// Structure that groups together the configuration of the video component
	struct Configuration
	{
		unsigned						startDelay;
		bool							showSnapshotNoVideo;
		bool							showSnapshotDelay;
		std::string						defaultVideoPath;
	};

public:
	VideoPlayerComponent(Window* window);
	virtual ~VideoPlayerComponent();

	// Loads the video at the given filepath
	bool setVideo(std::string path);
	// Loads a static image that is displayed if the video cannot be played
	void setImage(std::string path);

	// Configures the component to show the default video
	void setDefaultVideo();
	
	virtual void onShow() override;
	virtual void onHide() override;

	//Sets the origin as a percentage of this image (e.g. (0, 0) is top left, (0.5, 0.5) is the center)
	void setOrigin(float originX, float originY);
	inline void setOrigin(Eigen::Vector2f origin) { setOrigin(origin.x(), origin.y()); }

	void onSizeChanged() override;
	void setOpacity(unsigned char opacity) override;

	void render(const Eigen::Affine3f& parentTrans) override;

	virtual void applyTheme(const std::shared_ptr<ThemeData>& theme, const std::string& view, const std::string& element, unsigned int properties) override;

	virtual std::vector<HelpPrompt> getHelpPrompts() override;

	// Returns the center point of the video (takes origin into account).
	Eigen::Vector2f getCenter() const;

	virtual void update(int deltaTime);

private:
	// Start the video Immediately
	void startVideo();
	// Start the video after any configured delay
	void startVideoWithDelay();
	// Stop the video
	void stopVideo();

	// Handle any delay to the start of playing the video clip. Must be called periodically
	void handleStartDelay();

	// Handle looping the video. Must be called periodically
	void handleLooping();

	// Manage the playing state of the component
	void manageState();

private:
	unsigned						mVideoWidth;
	unsigned						mVideoHeight;
	Eigen::Vector2f 				mOrigin;
	std::string						mStaticImagePath;
	ImageComponent					mStaticImage;

	pid_t							mPlayerPid;

	boost::filesystem::path			mVideoPath;
	boost::filesystem::path			mPlayingVideoPath;
	bool							mStartDelayed;
	unsigned						mStartTime;
	bool							mIsPlaying;
	bool							mShowing;

	Configuration					mConfig;
};

#endif
