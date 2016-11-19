#include "components/VideoComponent.h"
#include "Renderer.h"
#include "ThemeData.h"
#include "Util.h"
#ifdef WIN32
#include <codecvt>
#endif

#define FADE_TIME_MS	200

libvlc_instance_t*		VideoComponent::mVLC = NULL;

// VLC prepares to render a video frame.
static void *lock(void *data, void **p_pixels) {
    struct VideoContext *c = (struct VideoContext *)data;
    SDL_LockMutex(c->mutex);
    SDL_LockSurface(c->surface);
	*p_pixels = c->surface->pixels;
    return NULL; // Picture identifier, not needed here.
}

// VLC just rendered a video frame.
static void unlock(void *data, void *id, void *const *p_pixels) {
    struct VideoContext *c = (struct VideoContext *)data;
    SDL_UnlockSurface(c->surface);
    SDL_UnlockMutex(c->mutex);
}

// VLC wants to display a video frame.
static void display(void *data, void *id) {
    //Data to be displayed
}

VideoComponent::VideoComponent(Window* window) : GuiComponent(window), mStaticImage(window),
	mMediaPlayer(nullptr), mVideoHeight(0), mVideoWidth(0),
	mStartDelay(0), mStartDelayed(false), mIsPlaying(false),
	mShowSnapshotDelay(false), mShowSnapshotNoVideo(false)
{
	memset(&mContext, 0, sizeof(mContext));

	// Get an empty texture for rendering the video
	mTexture = TextureResource::get("");

	// Make sure VLC has been initialised
	setupVLC();
}

VideoComponent::~VideoComponent()
{
	// Stop any currently running video
	setVideo("");
}

void VideoComponent::setOrigin(float originX, float originY)
{
	mOrigin << originX, originY;

	// Update the embeded static image
	mStaticImage.setOrigin(originX, originY);
}

Eigen::Vector2f VideoComponent::getCenter() const
{
	return Eigen::Vector2f(mPosition.x() - (getSize().x() * mOrigin.x()) + getSize().x() / 2,
		mPosition.y() - (getSize().y() * mOrigin.y()) + getSize().y() / 2);
}

void VideoComponent::onSizeChanged()
{
	// Update the embeded static image
	mStaticImage.onSizeChanged();
}

void VideoComponent::setVideo(std::string path)
{
	boost::filesystem::path fullPath = getCanonicalPath(path);
	fullPath.make_preferred().native();

	// Check that it's changed
	if (fullPath == mVideoPath)
		return;

	// See if the video was playing because we'll restart it if it was
	bool playing = mIsPlaying;
	
	// Stop current video
	stopVideo();
	mVideoPath.clear();

	// If the file exists then start the new video
	if (!fullPath.empty() && ResourceManager::getInstance()->fileExists(fullPath.generic_string()))
	{
		// Store the path
		mVideoPath = fullPath;

		// If there is a startup delay then the video will be started in the future
		// by the render() function otherwise start it now
		if (mStartDelay == 0)
		{
			mStartDelayed = false;
			// See if we need to start the new one playing
			if (playing) {
				startVideo();
			}
		}
		else
		{
			mStartDelayed = true;
			mFadeIn = 0.0f;
			mStartTime = SDL_GetTicks() + mStartDelay;
		}
	}
}

void VideoComponent::setImage(std::string path)
{
	// Check that the image has changed
	if (path == mStaticImagePath)
		return;
	
	mStaticImage.setImage(path);
	// Make the image stretch to fill the video region
	mStaticImage.setSize(getSize());
	mFadeIn = 0.0f;
	mStaticImagePath = path;
}

void VideoComponent::setDefaultVideo()
{
	setVideo(mDefaultVideoPath);
}

void VideoComponent::setOpacity(unsigned char opacity)
{
	mOpacity = opacity;
	// Update the embeded static image
	mStaticImage.setOpacity(opacity);
}

void VideoComponent::render(const Eigen::Affine3f& parentTrans)
{
	float x, y;

	Eigen::Affine3f trans = parentTrans * getTransform();
	GuiComponent::renderChildren(trans);

	Renderer::setMatrix(trans);
	
	handleStartDelay();
	handleLooping();

	if (mIsPlaying)
	{
		float tex_offs_x = 0.0f;
		float tex_offs_y = 0.0f;
		float x2;
		float y2;

		bool  maintain_aspect = false;
		bool  black_border = false;

		x = -(float)mSize.x() * mOrigin.x();
		y = -(float)mSize.y() * mOrigin.y();
		x2 = x+mSize.x();
		y2 = y+mSize.y();

		if (maintain_aspect) {
			if (!black_border) {
				tex_offs_x = (1.0f - (mVideoWidth / (float)mSize.x())) / 2.0f;
				tex_offs_y = (1.0f - (mVideoHeight / (float)mSize.y())) / 2.0f;
			}
			else {
				x = -(float)mVideoWidth * mOrigin.x();
				y = -(float)mVideoHeight * mOrigin.y();
				x2 = x + mVideoWidth;
				y2 = y + mVideoHeight;
			}
		}


		struct Vertex
		{
			Eigen::Vector2f pos;
			Eigen::Vector2f tex;
		} vertices[6];


		vertices[0].pos[0] = x; 			vertices[0].pos[1] = y;
		vertices[1].pos[0] = x; 			vertices[1].pos[1] = y2;
		vertices[2].pos[0] = x2;			vertices[2].pos[1] = y;

		vertices[3].pos[0] = x2;			vertices[3].pos[1] = y;
		vertices[4].pos[0] = x; 			vertices[4].pos[1] = y2;
		vertices[5].pos[0] = x2;			vertices[5].pos[1] = y2;

		vertices[0].tex[0] = -tex_offs_x; 			vertices[0].tex[1] = -tex_offs_y;
		vertices[1].tex[0] = -tex_offs_x; 			vertices[1].tex[1] = 1.0f + tex_offs_y;
		vertices[2].tex[0] = 1.0f + tex_offs_x;		vertices[2].tex[1] = -tex_offs_y;

		vertices[3].tex[0] = 1.0f + tex_offs_x;		vertices[3].tex[1] = -tex_offs_y;
		vertices[4].tex[0] = -tex_offs_x;			vertices[4].tex[1] = 1.0f + tex_offs_y;
		vertices[5].tex[0] = 1.0f + tex_offs_x;		vertices[5].tex[1] = 1.0f + tex_offs_y;

		glEnable(GL_TEXTURE_2D);

		glColor3f(mFadeIn, mFadeIn, mFadeIn);

		mTexture->initFromPixels((unsigned char*)mContext.surface->pixels, mContext.surface->w, mContext.surface->h);
		mTexture->bind();

		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);

		glVertexPointer(2, GL_FLOAT, sizeof(Vertex), &vertices[0].pos);
		glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), &vertices[0].tex);

		glDrawArrays(GL_TRIANGLES, 0, 6);

		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);

		glColor3f(1.0f, 1.0f, 1.0f);
		glDisable(GL_TEXTURE_2D);
	}
	else
	{
		if ((mShowSnapshotNoVideo && mVideoPath.empty()) || (mStartDelayed && mShowSnapshotDelay))
		{
			// Display the static image instead
			mStaticImage.setOpacity((unsigned char)(mFadeIn * 255.0f));
			glColor3f(mFadeIn, mFadeIn, mFadeIn);
			mStaticImage.render(parentTrans);
		}
	}

}

void VideoComponent::applyTheme(const std::shared_ptr<ThemeData>& theme, const std::string& view, const std::string& element, unsigned int properties)
{
	using namespace ThemeFlags;

	const ThemeData::ThemeElement* elem = theme->getElement(view, element, "video");
	if(!elem)
	{
		return;
	}

	Eigen::Vector2f scale = getParent() ? getParent()->getSize() : Eigen::Vector2f((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());

	if(properties & POSITION && elem->has("pos"))
	{
		Eigen::Vector2f denormalized = elem->get<Eigen::Vector2f>("pos").cwiseProduct(scale);
		setPosition(Eigen::Vector3f(denormalized.x(), denormalized.y(), 0));
	}

	if(properties & ThemeFlags::SIZE && elem->has("size"))
	{
		setSize(elem->get<Eigen::Vector2f>("size").cwiseProduct(scale));
	}

	// position + size also implies origin
	if((properties & ORIGIN || (properties & POSITION && properties & ThemeFlags::SIZE)) && elem->has("origin"))
		setOrigin(elem->get<Eigen::Vector2f>("origin"));

	if(elem->has("default"))
	{
		setDefaultVideoPath(elem->get<std::string>("default"));
	}

	if(properties & ThemeFlags::DELAY && elem->has("delay"))
	{
		setStartDelay(elem->get<float>("delay"));
	}
	if (elem->has("showSnapshotNoVideo"))
	{
		setShowSnapshotNoVideo(elem->get<bool>("showSnapshotNoVideo"));
	}
	if (elem->has("showSnapshotDelay"))
	{
		setShowSnapshotDelay(elem->get<bool>("showSnapshotDelay"));
	}
	// Update the embeded static image
	mStaticImage.setPosition(getPosition());
	mStaticImage.setMaxSize(getSize());
	mStaticImage.setSize(getSize());
}

std::vector<HelpPrompt> VideoComponent::getHelpPrompts()
{
	std::vector<HelpPrompt> ret;
	ret.push_back(HelpPrompt("a", "select"));
	return ret;
}

void VideoComponent::setupContext()
{
	if (!mContext.valid)
	{
		// Create an RGBA surface to render the video into
		mContext.surface = SDL_CreateRGBSurface(SDL_SWSURFACE, (int)mVideoWidth, (int)mVideoHeight, 32, 0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff);
		mContext.mutex = SDL_CreateMutex();
		mContext.valid = true;
	}
}

void VideoComponent::freeContext()
{
	if (mContext.valid)
	{
		SDL_FreeSurface(mContext.surface);
		SDL_DestroyMutex(mContext.mutex);
		mContext.valid = false;
	}
}

void VideoComponent::setupVLC()
{
	// If VLC hasn't been initialised yet then do it now
	if (!mVLC)
	{
		const char* args[] = { "--quiet" };
		mVLC = libvlc_new(sizeof(args) / sizeof(args[0]), args);
	}
}

void VideoComponent::setStartDelay(float seconds)
{
	mStartDelay = (unsigned)(seconds * 1000.0f);
}

void VideoComponent::setShowSnapshotNoVideo(bool show)
{
	mShowSnapshotNoVideo = show;
}

void VideoComponent::setShowSnapshotDelay(bool show)
{
	mShowSnapshotDelay = show;
}

void VideoComponent::setDefaultVideoPath(std::string path)
{
	mDefaultVideoPath = path;
}

void VideoComponent::handleStartDelay()
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
		startVideo();
	}
}

void VideoComponent::handleLooping()
{
	if (mIsPlaying && mMediaPlayer)
	{
		libvlc_state_t state = libvlc_media_player_get_state(mMediaPlayer);
		if (state == libvlc_Ended)
		{
			//libvlc_media_player_set_position(mMediaPlayer, 0.0f);
			libvlc_media_player_set_media(mMediaPlayer, mMedia);
			libvlc_media_player_play(mMediaPlayer);
		}
	}
}

void VideoComponent::startVideo()
{
	if (!mIsPlaying) {
		unsigned 	track_count;
		int			width = 0;
		int			height = 0;
		
#ifdef WIN32
		std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> wton;
		std::string path = wton.to_bytes(mVideoPath.c_str());
#else
		std::string path(mVideoPath.c_str());
#endif
		// Make sure we have a video path
		if (mVLC && (path.size() > 0))
		{
			// Open the media
			mMedia = libvlc_media_new_path(mVLC, path.c_str());
			if (mMedia)
			{
				// Get the media metadata so we can find the aspect ratio
				libvlc_media_parse(mMedia);
				libvlc_media_track_t** tracks;
				track_count = libvlc_media_tracks_get(mMedia, &tracks);
				for (unsigned track = 0; track < track_count; ++track)
				{
					if (tracks[track]->i_type == libvlc_track_video)
					{
						width = tracks[track]->video->i_width;
						height = tracks[track]->video->i_height;
						break;
					}
				}
				libvlc_media_tracks_release(tracks, track_count);

				// Work out the width and height of the video to fit in the window with
				// the correct aspect ratio
				float aspect_video = 1.0f;
				if ((width > 0) && (height > 0))
				{
					aspect_video = (float)width / (float)height;
				}
				if (aspect_video > 1.0f)
				{
					mVideoWidth = (unsigned)mSize.x();
					mVideoHeight = (unsigned)(mSize.x() / aspect_video);
				}
				else
				{
					mVideoHeight = (unsigned)mSize.y();
					mVideoWidth = (unsigned)(mSize.y() * aspect_video);
				}

				// Make sure the calculated size doesn't overflow the component size
				if (mVideoWidth > mSize.x()) {
					float ratio = (float)mVideoWidth / mSize.x();
					mVideoWidth = (unsigned)mSize.x();
					mVideoHeight = (unsigned)((float)mVideoHeight / ratio);
				}
				if (mVideoHeight > mSize.y()) {
					float ratio = (float)mVideoHeight / mSize.y();
					mVideoHeight = (unsigned)mSize.y();
					mVideoWidth = (unsigned)((float)mVideoWidth / ratio);
				}

				setupContext();

				// Setup the media player
				mMediaPlayer = libvlc_media_player_new_from_media(mMedia);
				libvlc_media_player_play(mMediaPlayer);
				libvlc_video_set_callbacks(mMediaPlayer, lock, unlock, display, (void*)&mContext);
				libvlc_video_set_format(mMediaPlayer, "RGBA", (int)mVideoWidth, (int)mVideoHeight, (int)mVideoWidth * 4);

				// Update the playing state
				mIsPlaying = true;
				mFadeIn = 0.0f;
			}
		}
	}
}

void VideoComponent::stopVideo()
{
	mIsPlaying = false;
	mStartDelayed = false;
	// Release the media player so it stops calling back to us
	if (mMediaPlayer)
	{
		libvlc_media_player_stop(mMediaPlayer);
		libvlc_media_player_release(mMediaPlayer);
		libvlc_media_release(mMedia);
		mMediaPlayer = NULL;
		freeContext();
	}
}

void VideoComponent::update(int deltaTime)
{
	// If the video start is delayed and there is less than the fade time then set the image fade
	// accordingly
	if (mStartDelayed)
	{
		ULONG ticks = SDL_GetTicks();
		if (mStartTime > ticks) 
		{
			ULONG diff = mStartTime - ticks;
			if (diff < FADE_TIME_MS) 
			{
				mFadeIn = (float)diff / (float)FADE_TIME_MS;
				return;
			}
		}
	}
	// If the fade in is less than 1 then increment it
	if (mFadeIn < 1.0f)
	{
		mFadeIn += deltaTime / (float)FADE_TIME_MS;
		if (mFadeIn > 1.0f)
			mFadeIn = 1.0f;
	}
	GuiComponent::update(deltaTime);
}
