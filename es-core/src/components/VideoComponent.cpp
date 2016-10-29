#include "components/VideoComponent.h"
#include "Renderer.h"
#include "ThemeData.h"

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

VideoComponent::VideoComponent(Window* window) : GuiComponent(window), mMediaPlayer(nullptr),
		mVideoHeight(0), mVideoWidth(0), mStartDelay(0), mStartDelayed(false), mIsPlaying(false)
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
}

Eigen::Vector2f VideoComponent::getCenter() const
{
	return Eigen::Vector2f(mPosition.x() - (getSize().x() * mOrigin.x()) + getSize().x() / 2,
		mPosition.y() - (getSize().y() * mOrigin.y()) + getSize().y() / 2);
}

void VideoComponent::onSizeChanged()
{
}

void VideoComponent::setVideo(std::string path)
{
	// See if the video was playing because we'll restart it if it was
	bool playing = mIsPlaying;
	
	// Stop current video
	stopVideo();
	mVideoPath.clear();

	// If the file exists then start the new video
	if (!path.empty() && ResourceManager::getInstance()->fileExists(path))
	{
		// Store the path
		mVideoPath = path;

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
			mStartTime = SDL_GetTicks() + mStartDelay;
		}
	}
}

void VideoComponent::setOpacity(unsigned char opacity)
{
	mOpacity = opacity;
}

void VideoComponent::render(const Eigen::Affine3f& parentTrans)
{
	float x, y, width, height;

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
		mTexture->initFromPixels((unsigned char*)mContext.surface->pixels, mContext.surface->w, mContext.surface->h);
		mTexture->bind();

		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);

		glVertexPointer(2, GL_FLOAT, sizeof(Vertex), &vertices[0].pos);
		glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), &vertices[0].tex);

		glDrawArrays(GL_TRIANGLES, 0, 6);

		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
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

	if(properties & PATH && elem->has("path"))
	{
		setVideo(elem->get<std::string>("path"));
	}

	if(properties & ThemeFlags::DELAY && elem->has("delay"))
	{
		setStartDelay(elem->get<float>("delay"));
	}
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
		char const *vlc_argv[] =
		{
		};
		int vlc_argc = sizeof(vlc_argv) / sizeof(*vlc_argv);
		mVLC = libvlc_new(vlc_argc, vlc_argv);
	}
}

void VideoComponent::setStartDelay(float seconds)
{
	mStartDelay = (unsigned)(seconds * 1000.0f);
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
		
		// Make sure we have a video path
		if (mVideoPath.size() > 0) {
			// Open the media
			mMedia = libvlc_media_new_path(mVLC, mVideoPath.c_str());

			// Get the media metadata so we can find the aspect ratio
			libvlc_media_parse(mMedia);
			libvlc_media_track_t** tracks;
			libvlc_media_track_t* track;
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
				mVideoWidth = mSize.x();
				mVideoHeight = mSize.x() / aspect_video;
			}
			else
			{
				mVideoHeight = mSize.y();
				mVideoWidth = mSize.y() * aspect_video;
			}

			// Make sure the calculated size doesn't overflow the component size
			if (mVideoWidth > mSize.x()) {
				float ratio = (float)mVideoWidth / mSize.x();
				mVideoWidth = mSize.x();
				mVideoHeight = (float)mVideoHeight / ratio;
			}
			if (mVideoHeight > mSize.y()) {
				float ratio = (float)mVideoHeight / mSize.y();
				mVideoHeight = mSize.y();
				mVideoWidth = (float)mVideoWidth / ratio;
			}

			setupContext();

			// Setup the media player
			mMediaPlayer = libvlc_media_player_new_from_media(mMedia);
			libvlc_media_player_play(mMediaPlayer);
			libvlc_video_set_callbacks(mMediaPlayer, lock, unlock, display, (void*)&mContext);
			libvlc_video_set_format(mMediaPlayer, "RGBA", (int)mVideoWidth, (int)mVideoHeight, (int)mVideoWidth*4);
			
			// Update the playing state
			mIsPlaying = true;
		}
	}
}

void VideoComponent::stopVideo()
{
	mIsPlaying = false;
	// Release the media player so it stops calling back to us
	if (mMediaPlayer)
	{
		mVideoPath.clear();
		libvlc_media_player_stop(mMediaPlayer);
		libvlc_media_player_release(mMediaPlayer);
		libvlc_media_release(mMedia);
		mMediaPlayer = NULL;
		freeContext();
	}
}

