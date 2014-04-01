

#include "dream.h"








MMAL_PORT_T *camera_preview_port = NULL;
MMAL_PORT_T *camera_video_port = NULL;
MMAL_PORT_T *camera_still_port = NULL;
MMAL_PORT_T *preview_input_port = NULL;
MMAL_PORT_T *encoder_input_port = NULL;
MMAL_PORT_T *encoder_output_port = NULL;




int FrameChanged=0;
IplImage *Frame;
IplImage *UsableFrame;

pthread_mutex_t Framemutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t UsableMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  Framecondition = PTHREAD_COND_INITIALIZER;
sem_t FrameReady;

pthread_t threadFrame;

void encoder_buffer_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
	printf("I AM IN CALLBACK \n");
   int complete = 0;

   // We pass our file handle and other stuff in via the userdata field.

   PORT_USERDATA *pData = (PORT_USERDATA *)port->userdata;
	
   if (pData)
   {
      if (buffer->length)
      {
         mmal_buffer_header_mem_lock(buffer);

		
		CvMat* buf = cvCreateMat(1,buffer->length,CV_8UC1);
		
		// copy buffer from cam to CvMat
		buf->data.ptr = buffer->data;
		// decode image (interpret jpg)
		
		
		Frame = cvDecodeImage(buf, 1);
		sem_post(&FrameReady);
		
		
		
		
	
  
         mmal_buffer_header_mem_unlock(buffer);
      }

      // Now flag if we have completed
      if (buffer->flags & (MMAL_BUFFER_HEADER_FLAG_FRAME_END | MMAL_BUFFER_HEADER_FLAG_TRANSMISSION_FAILED))
         complete = 1;
   }
   else
   {
      vcos_log_error("Received a encoder buffer callback with no state");
   }
   
   
   printf("I KEPT GOING");
   // release buffer back to the pool
   mmal_buffer_header_release(buffer);

   // and send one back to the port (if still open)
   if (port->is_enabled)
   {
		printf("PORT WAS ENABLED");
      MMAL_STATUS_T status;
      MMAL_BUFFER_HEADER_T *new_buffer;

      new_buffer = mmal_queue_get(pData->pstate->encoder_pool->queue);

      if (new_buffer)
      {
         status = mmal_port_send_buffer(port, new_buffer);
      }
      if (!new_buffer || status != MMAL_SUCCESS)
         vcos_log_error("Unable to return a buffer to the encoder port");
   }

   if (complete)
      vcos_semaphore_post(&(pData->complete_semaphore));

}


static void display_valid_parameters();
static void store_exif_tag(RASPISTILL_STATE *state, const char *exif_tag);

static struct
{
   char *format;
   MMAL_FOURCC_T encoding;
} encoding_xref[] =
{
   {"jpg", MMAL_ENCODING_JPEG},
   {"bmp", MMAL_ENCODING_BMP},
   {"gif", MMAL_ENCODING_GIF},
   {"png", MMAL_ENCODING_PNG}
};


static int encoding_xref_size = sizeof(encoding_xref) / sizeof(encoding_xref[0]);
/**
 * Assign a default set of parameters to the state passed in
 *
 * @param state Pointer to state structure to assign defaults to
 */
static void default_status(RASPISTILL_STATE *state)
{
   if (!state)
   {
      vcos_assert(0);
      return;
   }
	
	// *** PR : modif for demo purpose : smaller image
   
}


/**
 *  buffer header callback function for camera control
 *
 *  No actions taken in current version
 *
 * @param port Pointer to port from which callback originated
 * @param buffer mmal buffer header pointer
 */
static void camera_control_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
   if (buffer->cmd == MMAL_EVENT_PARAMETER_CHANGED)
   {
   }
   else
   {
      vcos_log_error("Received unexpected camera control callback event, 0x%08x", buffer->cmd);
   }

   mmal_buffer_header_release(buffer);
}


/**
 *  buffer header callback function for encoder
 *
 *  Callback will dump buffer data to the specific file
 *
 * @param port Pointer to port from which callback originated
 * @param buffer mmal buffer header pointer
 */



/**
 * Create the camera component, set up its ports
 *
 * @param state Pointer to state control struct
 *
 * @return 0 if failed, pointer to component if successful
 *
 */
static MMAL_COMPONENT_T *create_camera_component(RASPISTILL_STATE *state)
{
   MMAL_COMPONENT_T *camera = 0;
   MMAL_ES_FORMAT_T *format;
   MMAL_PORT_T *preview_port = NULL, *video_port = NULL, *still_port = NULL;
   MMAL_STATUS_T status;

   /* Create the component */
   status = mmal_component_create(MMAL_COMPONENT_DEFAULT_CAMERA, &camera);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Failed to create camera component");
      goto error;
   }

   if (!camera->output_num)
   {
      vcos_log_error("Camera doesn't have output ports");
      goto error;
   }

   preview_port = camera->output[MMAL_CAMERA_PREVIEW_PORT];
   video_port = camera->output[MMAL_CAMERA_VIDEO_PORT];
   still_port = camera->output[MMAL_CAMERA_CAPTURE_PORT];


   // Enable the camera, and tell it its control callback function
   status = mmal_port_enable(camera->control, camera_control_callback);

   if (status)
   {
      vcos_log_error("Unable to enable control port : error %d", status);
      goto error;
   }

   //  set up the camera configuration
   {
      MMAL_PARAMETER_CAMERA_CONFIG_T cam_config =
      {
         { MMAL_PARAMETER_CAMERA_CONFIG, sizeof(cam_config) },
         .max_stills_w = state->width,
         .max_stills_h = state->height,
         .stills_yuv422 = 0,
         .one_shot_stills = 1,
         .max_preview_video_w = state->preview_parameters.previewWindow.width,
         .max_preview_video_h = state->preview_parameters.previewWindow.height,
         .num_preview_video_frames = 3,
         .stills_capture_circular_buffer_height = 0,
         .fast_preview_resume = 0,
         .use_stc_timestamp = MMAL_PARAM_TIMESTAMP_MODE_RESET_STC
      };
      mmal_port_parameter_set(camera->control, &cam_config.hdr);
   }

   raspicamcontrol_set_all_parameters(camera, &state->camera_parameters);

   // Now set up the port formats

   format = preview_port->format;

   format->encoding = MMAL_ENCODING_OPAQUE;
   format->encoding_variant = MMAL_ENCODING_I420;

   format->es->video.width = state->preview_parameters.previewWindow.width;
   format->es->video.height = state->preview_parameters.previewWindow.height;
   format->es->video.crop.x = 0;
   format->es->video.crop.y = 0;
   format->es->video.crop.width = state->preview_parameters.previewWindow.width;
   format->es->video.crop.height =state->preview_parameters.previewWindow.height;
   format->es->video.frame_rate.num = PREVIEW_FRAME_RATE_NUM;
   format->es->video.frame_rate.den = PREVIEW_FRAME_RATE_DEN;
	
   status = mmal_port_format_commit(preview_port);

   if (status)
   {
      vcos_log_error("camera viewfinder format couldn't be set");
      goto error;
   }

   // Set the same format on the video  port (which we dont use here)
   mmal_format_full_copy(video_port->format, format);
   status = mmal_port_format_commit(video_port);

   if (status)
   {
      vcos_log_error("camera video format couldn't be set");
      goto error;
   }

   // Ensure there are enough buffers to avoid dropping frames
   if (video_port->buffer_num < VIDEO_OUTPUT_BUFFERS_NUM)
      video_port->buffer_num = VIDEO_OUTPUT_BUFFERS_NUM;

   format = still_port->format;

   // Set our stills format on the stills (for encoder) port
   format->encoding = MMAL_ENCODING_OPAQUE;
   format->es->video.width = state->width;
   format->es->video.height = state->height;
   format->es->video.crop.x = 0;
   format->es->video.crop.y = 0;
   format->es->video.crop.width = state->width;
   format->es->video.crop.height = state->height;
   format->es->video.frame_rate.num = STILLS_FRAME_RATE_NUM;
   format->es->video.frame_rate.den = STILLS_FRAME_RATE_DEN;

   status = mmal_port_format_commit(still_port);

   if (status)
   {
      vcos_log_error("camera still format couldn't be set");
      goto error;
   }

   /* Ensure there are enough buffers to avoid dropping frames */
   if (still_port->buffer_num < VIDEO_OUTPUT_BUFFERS_NUM)
      still_port->buffer_num = VIDEO_OUTPUT_BUFFERS_NUM;

   /* Enable component */
   status = mmal_component_enable(camera);

   if (status)
   {
      vcos_log_error("camera component couldn't be enabled");
      goto error;
   }

   if (state->wantRAW)
   {
      if (mmal_port_parameter_set_boolean(still_port, MMAL_PARAMETER_ENABLE_RAW_CAPTURE, 1) != MMAL_SUCCESS)
      {
         vcos_log_error("RAW was requested, but failed to enable");

         // Continue on and take picture without.
      }
   }

   state->camera_component = camera;

   if (state->verbose)
      fprintf(stderr, "Camera component done\n");

   return camera;

error:

   if (camera)
      mmal_component_destroy(camera);

   return 0;
}

/**
 * Destroy the camera component
 *
 * @param state Pointer to state control struct
 *
 */
static void destroy_camera_component(RASPISTILL_STATE *state)
{
   if (state->camera_component)
   {
      mmal_component_destroy(state->camera_component);
      state->camera_component = NULL;
   }
}



/**
 * Create the encoder component, set up its ports
 *
 * @param state Pointer to state control struct
 *
 * @return 0 if failed, pointer to component if successful
 *
 */
static MMAL_COMPONENT_T *create_encoder_component(RASPISTILL_STATE *state)
{
   MMAL_COMPONENT_T *encoder = 0;
   MMAL_PORT_T *encoder_input = NULL, *encoder_output = NULL;
   MMAL_STATUS_T status;
   MMAL_POOL_T *pool;

   status = mmal_component_create(MMAL_COMPONENT_DEFAULT_IMAGE_ENCODER, &encoder);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Unable to create JPEG encoder component");
      goto error;
   }

   if (!encoder->input_num || !encoder->output_num)
   {
      vcos_log_error("JPEG encoder doesn't have input/output ports");
      goto error;
   }

   encoder_input = encoder->input[0];
   encoder_output = encoder->output[0];

   // We want same format on input and output
   mmal_format_copy(encoder_output->format, encoder_input->format);

   // Specify out output format
   encoder_output->format->encoding = state->encoding;

   encoder_output->buffer_size = encoder_output->buffer_size_recommended;

   if (encoder_output->buffer_size < encoder_output->buffer_size_min)
      encoder_output->buffer_size = encoder_output->buffer_size_min;

   encoder_output->buffer_num = encoder_output->buffer_num_recommended;

   if (encoder_output->buffer_num < encoder_output->buffer_num_min)
      encoder_output->buffer_num = encoder_output->buffer_num_min;

   // Commit the port changes to the output port
   status = mmal_port_format_commit(encoder_output);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Unable to set format on video encoder output port");
      goto error;
   }

   // Set the JPEG quality level
   status = mmal_port_parameter_set_uint32(encoder_output, MMAL_PARAMETER_JPEG_Q_FACTOR, state->quality);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Unable to set JPEG quality");
      goto error;
   }

      //  Enable component
   status = mmal_component_enable(encoder);

   if (status)
   {
      vcos_log_error("Unable to enable video encoder component");
      goto error;
   }

   /* Create pool of buffer headers for the output port to consume */
   pool = mmal_port_pool_create(encoder_output, encoder_output->buffer_num, encoder_output->buffer_size);

   if (!pool)
   {
      vcos_log_error("Failed to create buffer header pool for encoder output port %s", encoder_output->name);
   }

   state->encoder_pool = pool;
   state->encoder_component = encoder;

   if (state->verbose)
      fprintf(stderr, "Encoder component done\n");

   return encoder;

   error:
   if (encoder)
      mmal_component_destroy(encoder);

   return 0;
}

/**
 * Destroy the encoder component
 *
 * @param state Pointer to state control struct
 *
 */
static void destroy_encoder_component(RASPISTILL_STATE *state)
{
   // Get rid of any port buffers first
   if (state->encoder_pool)
   {
      mmal_port_pool_destroy(state->encoder_component->output[0], state->encoder_pool);
   }

   if (state->encoder_component)
   {
      mmal_component_destroy(state->encoder_component);
      state->encoder_component = NULL;
   }
}

/**
 * Add an exif tag to the capture
 *
 * @param state Pointer to state control struct
 * @param exif_tag String containing a "key=value" pair.
 * @return  Returns a MMAL_STATUS_T giving result of operation
 */
static MMAL_STATUS_T add_exif_tag(RASPISTILL_STATE *state, const char *exif_tag)
{
   MMAL_STATUS_T status;
   MMAL_PARAMETER_EXIF_T *exif_param = (MMAL_PARAMETER_EXIF_T*)calloc(sizeof(MMAL_PARAMETER_EXIF_T) + MAX_EXIF_PAYLOAD_LENGTH, 1);

   vcos_assert(state);
   vcos_assert(state->encoder_component);

   // Check to see if the tag is present or is indeed a key=value pair.
   if (!exif_tag || strchr(exif_tag, '=') == NULL || strlen(exif_tag) > MAX_EXIF_PAYLOAD_LENGTH-1)
      return MMAL_EINVAL;

   exif_param->hdr.id = MMAL_PARAMETER_EXIF;

   strncpy((char*)exif_param->data, exif_tag, MAX_EXIF_PAYLOAD_LENGTH-1);

   exif_param->hdr.size = sizeof(MMAL_PARAMETER_EXIF_T) + strlen((char*)exif_param->data);

   status = mmal_port_parameter_set(state->encoder_component->output[0], &exif_param->hdr);

   free(exif_param);

   return status;
}
/**
 * Connect two specific ports together
 *
 * @param output_port Pointer the output port
 * @param input_port Pointer the input port
 * @param Pointer to a mmal connection pointer, reassigned if function successful
 * @return Returns a MMAL_STATUS_T giving result of operation
 *
 */
static MMAL_STATUS_T connect_ports(MMAL_PORT_T *output_port, MMAL_PORT_T *input_port, MMAL_CONNECTION_T **connection)
{
   MMAL_STATUS_T status;

   status =  mmal_connection_create(connection, output_port, input_port, MMAL_CONNECTION_FLAG_TUNNELLING | MMAL_CONNECTION_FLAG_ALLOCATION_ON_INPUT);

   if (status == MMAL_SUCCESS)
   {
      status =  mmal_connection_enable(*connection);
      if (status != MMAL_SUCCESS)
         mmal_connection_destroy(*connection);
   }

   return status;
}

/**
 * Checks if specified port is valid and enabled, then disables it
 *
 * @param port  Pointer the port
 *
 */
static void check_disable_port(MMAL_PORT_T *port)
{
   if (port && port->is_enabled)
      mmal_port_disable(port);
}

/**
 * Handler for sigint signals
 *
 * @param signal_number ID of incoming signal.
 *
 */
static void signal_handler(int signal_number)
{
   // Going to abort on all signals
   vcos_log_error("Aborting program\n");

   // Need to close any open stuff...

   exit(255);
}

void WaitForFrame()
{
	if (state.timelapse)
	  vcos_sleep(state.timelapse);
	else
	  vcos_sleep(state.timeout);

	  // Send all the buffers to the encoder output port
	  int num = mmal_queue_length(state.encoder_pool->queue);
	  int q;

	  for (q=0;q<num;q++)
	  {
		 MMAL_BUFFER_HEADER_T *buffer = mmal_queue_get(state.encoder_pool->queue);

		 if (!buffer)
			vcos_log_error("Unable to get a required buffer %d from pool queue", q);

		 if (mmal_port_send_buffer(encoder_output_port, buffer)!= MMAL_SUCCESS)
			vcos_log_error("Unable to send a buffer to encoder output port (%d)", q);
	  }
	  
	  if (mmal_port_parameter_set_boolean(camera_still_port, MMAL_PARAMETER_CAPTURE, 1) != MMAL_SUCCESS)
	  {
		 vcos_log_error("%s: Failed to start capture", __func__);
	  }
	  else
	  {
		 // Wait for capture to complete
		 // For some reason using vcos_semaphore_wait_timeout sometimes returns immediately with bad parameter error
		 // even though it appears to be all correct, so reverting to untimed one until figure out why its erratic
		 
		vcos_semaphore_wait(&callback_data.complete_semaphore);
			

	  }    
	  
	  
}

void SetUpCamera(Params Param)
{
	MMAL_STATUS_T status = -1;
	extern RASPISTILL_STATE state;
	extern PORT_USERDATA callback_data;
	state.timeout = Param.timeout; 
	state.width = Param.width;
	state.height = Param.height; 
	state.quality = Param.quality;
	state.wantRAW = Param.wantRaw;
	state.filename = NULL;
	state.verbose = 0;
	state.thumbnailConfig.enable = 1;
	state.thumbnailConfig.width = 64;
	state.thumbnailConfig.height = 48;
	state.thumbnailConfig.quality = 35;
	state.demoMode = 0;
	state.demoInterval = 250; // ms
	state.camera_component = NULL;
	state.encoder_component = NULL;
	state.preview_connection = NULL;
	state.encoder_connection = NULL;
	state.encoder_pool = NULL;
	state.encoding = MMAL_ENCODING_JPEG;
	state.numExifTags = 0;
	state.timelapse = 0;

	bcm_host_init();	
		
	

	


	signal(SIGINT, signal_handler);

	// Setup preview window defaults
	raspipreview_set_defaults(&state.preview_parameters);

	// Set up the camera_parameters to default
	raspicamcontrol_set_defaults(&state.camera_parameters);

	if (!create_camera_component(&state))
	{
		vcos_log_error("%s: Failed to create camera component", __func__);
	}
	else if ( (status = raspipreview_create(&state.preview_parameters)) != MMAL_SUCCESS)
	{
		vcos_log_error("%s: Failed to create preview component", __func__);
		destroy_camera_component(&state);
	}
	else if (!create_encoder_component(&state))
	{
		vcos_log_error("%s: Failed to create encode component", __func__);
		raspipreview_destroy(&state.preview_parameters);
		destroy_camera_component(&state);
	}
	else
	{
		
		
		// *** PR : we don't want preview
		camera_preview_port = NULL;//state.camera_component->output[MMAL_CAMERA_PREVIEW_PORT];
		camera_video_port   = state.camera_component->output[MMAL_CAMERA_VIDEO_PORT];
		camera_still_port   = state.camera_component->output[MMAL_CAMERA_CAPTURE_PORT];
		preview_input_port  = state.preview_parameters.preview_component->input[0];
		encoder_input_port  = state.encoder_component->input[0];
		encoder_output_port = state.encoder_component->output[0];


		VCOS_STATUS_T vcos_status;

		if (state.verbose)
		fprintf(stderr, "Connecting camera stills port to encoder input port\n");

		// Now connect the camera to the encoder
		status = connect_ports(camera_still_port, encoder_input_port, &state.encoder_connection);

		if (status != MMAL_SUCCESS)
		{
			vcos_log_error("%s: Failed to connect camera video port to encoder input", __func__);
			exit(1);
		}

		// Set up our userdata - this is passed though to the callback where we need the information.
		// Null until we open our filename
		callback_data.file_handle = NULL;
		callback_data.pstate = &state;
		vcos_status = vcos_semaphore_create(&callback_data.complete_semaphore, "RaspiStill-sem", 0);

		vcos_assert(vcos_status == VCOS_SUCCESS);

		encoder_output_port->userdata = (struct MMAL_PORT_USERDATA_T *)&callback_data;

		if (state.verbose)
			fprintf(stderr, "Enabling encoder output port\n");

		// Enable the encoder output port and tell it its callback function
		status = mmal_port_enable(encoder_output_port, encoder_buffer_callback);

		if (status != MMAL_SUCCESS)
		{
			vcos_log_error("Failed to setup encoder output");
			exit(1);
		}
	}
}

int SetFrame()
{
	printf("I AM WAITING");
	sem_wait(&FrameReady);
	printf("NOW I STARTED");
	UsableFrame=cvCloneImage(Frame);
	printf("READY");
	
	

}
void WaitFrame()
{
	while(1)
	{
		WaitForFrame();
		
	}
}