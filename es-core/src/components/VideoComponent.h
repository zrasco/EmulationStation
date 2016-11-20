#ifndef _VIDEOCOMPONENT_H_
#define _VIDEOCOMPONENT_H_

#include "platform.h"
#include GLHEADER

#include "GuiComponent.h"
#include "ImageComponent.h"
#include <string>
#include <memory>
#include "resources/TextureResource.h"
#include <vlc/vlc.h>
#include <SDL.h>
#include <SDL_mutex.h>
#include <boost/filesystem.hpp>

struct VideoContext {
	SDL_Surface*		surface;
	SDL_mutex*			mutex;
	bool				valid;
};

class VideoComponent : public GuiComponent
{
public:
	static void setupVLC();

	VideoComponent(Window* window);
	virtual ~VideoComponent();

	// Loads the video at the given filepath
	bool setVideo(std::string path);
	// Loads a static image that is displayed if the video cannot be played
	void setImage(std::string path);

	// Configures the component to show the default video
	void setDefaultVideo();
	
	// Start the video
	void startVideo();
	// Stop the video
	void stopVideo();

	//Sets the origin as a percentage of this image (e.g. (0, 0) is top left, (0.5, 0.5) is the center)
	void setOrigin(float originX, float originY);
	inline void setOrigin(Eigen::Vector2f origin) { setOrigin(origin.x(), origin.y()); }

	// Theme configuration
	void setStartDelay(float seconds);
	void setShowSnapshotNoVideo(bool show);
	void setShowSnapshotDelay(bool show);
	void setDefaultVideoPath(std::string path);

	void onSizeChanged() override;
	void setOpacity(unsigned char opacity) override;

	void render(const Eigen::Affine3f& parentTrans) override;

	virtual void applyTheme(const std::shared_ptr<ThemeData>& theme, const std::string& view, const std::string& element, unsigned int properties) override;

	virtual std::vector<HelpPrompt> getHelpPrompts() override;

	// Returns the center point of the video (takes origin into account).
	Eigen::Vector2f getCenter() const;

	virtual void update(int deltaTime);

private:
	void setupContext();
	void freeContext();

	// Handle any delay to the start of playing the video clip. Must be called periodically
	void handleStartDelay();

	// Handle looping the video. Must be called periodically
	void handleLooping();

private:
	static libvlc_instance_t*		mVLC;
	libvlc_media_t*					mMedia;
	libvlc_media_player_t*			mMediaPlayer;
	VideoContext					mContext;
	unsigned						mVideoWidth;
	unsigned						mVideoHeight;
	Eigen::Vector2f 				mOrigin;
	unsigned						mStartDelay;
	bool							mShowSnapshotNoVideo;
	bool							mShowSnapshotDelay;
	std::string						mDefaultVideoPath;
	boost::filesystem::path			mVideoPath;
	bool							mStartDelayed;
	unsigned						mStartTime;
	bool							mIsPlaying;
	std::shared_ptr<TextureResource> mTexture;
	float							mFadeIn;
	std::string						mStaticImagePath;

	ImageComponent					mStaticImage;
};

#endif
