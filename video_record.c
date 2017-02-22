/* 
 * File:   buffer_demo.c
 * Author: Tasanakorn
 *
 * Created on May 22, 2013, 1:52 PM
 *
 * Modified by: pcpark<pcparklin@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <signal.h>
#include <pthread.h>

#include "bcm_host.h"
#include "interface/vcos/vcos.h"

#include "interface/mmal/mmal.h"
#include "interface/mmal/util/mmal_default_components.h"
#include "interface/mmal/util/mmal_connection.h"

#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>
#include <sqlite3.h>

#define MMAL_CAMERA_PREVIEW_PORT 0
#define MMAL_CAMERA_VIDEO_PORT 1
#define MMAL_CAMERA_CAPTURE_PORT 2

#define VIDEO_FPS 25 
#define VIDEO_WIDTH 1280
#define VIDEO_HEIGHT 720
#define VIDEO_BITRATE 1700000
#define OVERLAY_WIDTH 1280
#define OVERLAY_HEIGHT 25
#define T_OVERLAY_WIDTH 220
#define T_OVERLAY_HEIGHT 20

typedef struct {
    int width;
    int height;
    int bitrate;
    int framerate;
    int intraperiod;
    int overlay_width;
    int overlay_height;
    int overlay_font_size;
    MMAL_COMPONENT_T *camera;
    MMAL_COMPONENT_T *encoder;
    MMAL_COMPONENT_T *preview;
    MMAL_PORT_T *camera_preview_port;
    MMAL_PORT_T *camera_video_port;
    MMAL_PORT_T *camera_still_port;
    MMAL_POOL_T *camera_video_port_pool;
    MMAL_PORT_T *encoder_input_port;
    MMAL_POOL_T *encoder_input_pool;
    MMAL_PORT_T *encoder_output_port;
    MMAL_POOL_T *encoder_output_pool;
    uint8_t *overlay_buffer;
    uint8_t *overlay_buffer2;
    uint8_t *t_overlay_buffer;
    uint8_t *t_overlay_buffer2;
    int overlay;
    int overlay_x;
    int overlay_y;
    char *overlay_text;
    char *db_path;
    int sync_overlay;
    int overlay_py;
    int overlay_pu;
    int overlay_pv;
    int show_display;
    int custom_font;
    // float fps;
} PORT_USERDATA;

static void camera_video_buffer_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer) {
    uint8_t *local_overlay_buffer;
    uint8_t *local_t_overlay_buffer;

    MMAL_BUFFER_HEADER_T *new_buffer;
    MMAL_BUFFER_HEADER_T *output_buffer = 0;
    PORT_USERDATA *userdata = (PORT_USERDATA *) port->userdata;

    MMAL_POOL_T *pool = userdata->camera_video_port_pool;


    output_buffer = mmal_queue_get(userdata->encoder_input_pool->queue);

    //Set pointer to  latest updated/drawn double buffer to local pointer  
    if (userdata->overlay == 0) {
        local_overlay_buffer = userdata->overlay_buffer;
        local_t_overlay_buffer = userdata->t_overlay_buffer;
    }
    else {
        local_overlay_buffer = userdata->overlay_buffer2;
        local_t_overlay_buffer = userdata->t_overlay_buffer2;
    }

    //Try to some colors http://en.wikipedia.org/wiki/YUV
    int chrominance_offset = userdata->width * userdata->height;
    int v_offset = chrominance_offset / 4;
    int chroma = 0;

    if (output_buffer) {
        mmal_buffer_header_mem_lock(buffer);
        memcpy(output_buffer->data, buffer->data, buffer->length);
        // dim
        int x, y;
        // update overlay info
        for (x = 0; x < userdata->overlay_width; x++) {
            for (y = 0; y < userdata->overlay_height; y++) {
                if (local_overlay_buffer[(y * userdata->overlay_width + x) * 4] > 0 &&
                    (userdata->overlay_y+y) < userdata->height) {
                    //copy luma Y
                    output_buffer->data[(userdata->overlay_y+y) * userdata->width + x ] = userdata->overlay_py;
                    //pointer to chrominance U/V
                    chroma= (userdata->overlay_y+y) / 2 * userdata->width / 2 + x / 2 + chrominance_offset;
                    //just guessing colors 
                    output_buffer->data[chroma] = userdata->overlay_pu;
                    output_buffer->data[chroma+v_offset] = userdata->overlay_pv;
                }
            }
        }

        // update time info
        for (x = 0; x < T_OVERLAY_WIDTH; x++) {
            for (y = 0; y < T_OVERLAY_HEIGHT; y++) {
                if (local_t_overlay_buffer[(y * T_OVERLAY_WIDTH + x) * 4] > 0 && T_OVERLAY_HEIGHT < userdata->height) {
                    //copy luma Y
                    output_buffer->data[y * userdata->width + x ] = 223;
                    //pointer to chrominance U/V
                    chroma= y / 2 * userdata->width / 2 + x / 2 + chrominance_offset;
                    //just guessing colors 
                    output_buffer->data[chroma] = 56;
                    output_buffer->data[chroma+v_offset] = 184;
                }
            }
        }

        output_buffer->length = buffer->length;
        mmal_buffer_header_mem_unlock(buffer);
        if (mmal_port_send_buffer(userdata->encoder_input_port, output_buffer) != MMAL_SUCCESS) {
            fprintf(stderr, "ERROR: Unable to send buffer \n");
        }
    } else {
        fprintf(stderr, "ERROR: mmal_queue_get (%d)\n", output_buffer);
    }

    mmal_buffer_header_release(buffer);

    // and send one back to the port (if still open)
    if (port->is_enabled) {
        MMAL_STATUS_T status;

        new_buffer = mmal_queue_get(pool->queue);

        if (new_buffer) {
            status = mmal_port_send_buffer(port, new_buffer);
        }

        if (!new_buffer || status != MMAL_SUCCESS) {
            fprintf(stderr, "Error: Unable to return a buffer to the video port\n");
        }
    }
}

static void encoder_input_buffer_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer) {
    //fprintf(stderr, "INFO:%s\n", __func__);    
    mmal_buffer_header_release(buffer);
}

static void encoder_output_buffer_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer) {
    MMAL_BUFFER_HEADER_T *new_buffer;
    PORT_USERDATA *userdata = (PORT_USERDATA *) port->userdata;
    MMAL_POOL_T *pool = userdata->encoder_output_pool;
    //fprintf(stderr, "INFO:%s\n", __func__);

    mmal_buffer_header_mem_lock(buffer);
    fwrite(buffer->data, 1, buffer->length, stdout);
    mmal_buffer_header_mem_unlock(buffer);

    mmal_buffer_header_release(buffer);
    if (port->is_enabled) {
        MMAL_STATUS_T status;

        new_buffer = mmal_queue_get(pool->queue);

        if (new_buffer) {
            status = mmal_port_send_buffer(port, new_buffer);
        }

        if (!new_buffer || status != MMAL_SUCCESS) {
            fprintf(stderr, "Unable to return a buffer to the video port\n");
        }
    }
}

int fill_port_buffer(MMAL_PORT_T *port, MMAL_POOL_T *pool) {
    int q;
    int num = mmal_queue_length(pool->queue);

    for (q = 0; q < num; q++) {
        MMAL_BUFFER_HEADER_T *buffer = mmal_queue_get(pool->queue);
        if (!buffer) {
            fprintf(stderr, "Unable to get a required buffer %d from pool queue\n", q);
        }

        if (mmal_port_send_buffer(port, buffer) != MMAL_SUCCESS) {
            fprintf(stderr, "Unable to send a buffer to port (%d)\n", q);
        }
    }
}

int setup_camera(PORT_USERDATA *userdata) {
    MMAL_STATUS_T status;
    MMAL_COMPONENT_T *camera = 0;
    MMAL_ES_FORMAT_T *format;
    MMAL_PORT_T * camera_preview_port;
    MMAL_PORT_T * camera_video_port;
    MMAL_PORT_T * camera_still_port;
    MMAL_POOL_T * camera_video_port_pool;

    status = mmal_component_create(MMAL_COMPONENT_DEFAULT_CAMERA, &camera);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: create camera %x\n", status);
        return -1;
    }
    userdata->camera = camera;
    userdata->camera_preview_port = camera->output[MMAL_CAMERA_PREVIEW_PORT];
    userdata->camera_video_port = camera->output[MMAL_CAMERA_VIDEO_PORT];
    userdata->camera_still_port = camera->output[MMAL_CAMERA_CAPTURE_PORT];

    camera_preview_port = camera->output[MMAL_CAMERA_PREVIEW_PORT];
    camera_video_port = camera->output[MMAL_CAMERA_VIDEO_PORT];
    camera_still_port = camera->output[MMAL_CAMERA_CAPTURE_PORT];


    {
        MMAL_PARAMETER_CAMERA_CONFIG_T cam_config = {
            { MMAL_PARAMETER_CAMERA_CONFIG, sizeof (cam_config)},
            .max_stills_w = 1280,
            .max_stills_h = 720,
            .stills_yuv422 = 0,
            .one_shot_stills = 1,
            .max_preview_video_w = userdata->width,
            .max_preview_video_h = userdata->height,
            .num_preview_video_frames = 3,
            .stills_capture_circular_buffer_height = 0,
            .fast_preview_resume = 0,
            .use_stc_timestamp = MMAL_PARAM_TIMESTAMP_MODE_RESET_STC
        };
        mmal_port_parameter_set(camera->control, &cam_config.hdr);
    }

    // Setup camera preview port format 
    format = camera_preview_port->format;
    format->encoding = MMAL_ENCODING_OPAQUE;
    format->encoding_variant = MMAL_ENCODING_I420;
    format->es->video.width = userdata->width;
    format->es->video.height = userdata->height;
    format->es->video.crop.x = 0;
    format->es->video.crop.y = 0;
    format->es->video.crop.width = userdata->width;
    format->es->video.crop.height = userdata->height;

    status = mmal_port_format_commit(camera_preview_port);

    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: camera viewfinder format couldn't be set\n");
        return -1;
    }

    // Setup camera video port format
    mmal_format_copy(camera_video_port->format, camera_preview_port->format);

    format = camera_video_port->format;
    format->encoding = MMAL_ENCODING_I420;
    format->encoding_variant = MMAL_ENCODING_I420;
    format->es->video.width = userdata->width;
    format->es->video.height = userdata->height;
    format->es->video.crop.x = 0;
    format->es->video.crop.y = 0;
    format->es->video.crop.width = userdata->width;
    format->es->video.crop.height = userdata->height;
    format->es->video.frame_rate.num = userdata->framerate;
    format->es->video.frame_rate.den = 1;

    camera_video_port->buffer_size = format->es->video.width * format->es->video.height * 12 / 8;
    camera_video_port->buffer_num = 2;

    fprintf(stderr, "INFO:camera video buffer_size = %d\n", camera_video_port->buffer_size);
    fprintf(stderr, "INFO:camera video buffer_num = %d\n", camera_video_port->buffer_num);

    status = mmal_port_format_commit(camera_video_port);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: unable to commit camera video port format (%u)\n", status);
        return -1;
    }

    camera_video_port_pool = (MMAL_POOL_T *) mmal_port_pool_create(camera_video_port, camera_video_port->buffer_num, camera_video_port->buffer_size);
    userdata->camera_video_port_pool = camera_video_port_pool;
    camera_video_port->userdata = (struct MMAL_PORT_USERDATA_T *) userdata;


    status = mmal_port_enable(camera_video_port, camera_video_buffer_callback);

    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: unable to enable camera video port (%u)\n", status);
        return -1;
    }

    status = mmal_component_enable(camera);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: unable to enable camera (%u)\n", status);
        return -1;
    }


    fill_port_buffer(userdata->camera_video_port, userdata->camera_video_port_pool);

    if (mmal_port_parameter_set_boolean(camera_video_port, MMAL_PARAMETER_CAPTURE, 1) != MMAL_SUCCESS) {
        printf("%s: Failed to start capture\n", __func__);
    }

    fprintf(stderr, "INFO: camera created\n");
    return 0;
}

int setup_encoder(PORT_USERDATA *userdata) {
    MMAL_STATUS_T status;
    MMAL_COMPONENT_T *encoder = 0;
    MMAL_PORT_T *preview_input_port = NULL;

    MMAL_PORT_T *encoder_input_port = NULL, *encoder_output_port = NULL;
    MMAL_POOL_T *encoder_input_port_pool;
    MMAL_POOL_T *encoder_output_port_pool;

    status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_ENCODER, &encoder);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: unable to create preview (%u)\n", status);
        return -1;
    }

    encoder_input_port = encoder->input[0];
    encoder_output_port = encoder->output[0];
    userdata->encoder_input_port = encoder_input_port;
    userdata->encoder_output_port = encoder_input_port;

    mmal_format_copy(encoder_input_port->format, userdata->camera_video_port->format);
    encoder_input_port->buffer_size = encoder_input_port->buffer_size_recommended;
    encoder_input_port->buffer_num = 2;


    mmal_format_copy(encoder_output_port->format, encoder_input_port->format);

    encoder_output_port->buffer_size = encoder_output_port->buffer_size_recommended;
    encoder_output_port->buffer_num = 2;
    // Commit the port changes to the input port 
    status = mmal_port_format_commit(encoder_input_port);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: unable to commit encoder input port format (%u)\n", status);
        return -1;
    }

    // Only supporting H264 at the moment
    encoder_output_port->format->encoding = MMAL_ENCODING_H264;
    encoder_output_port->format->bitrate = userdata->bitrate;

    encoder_output_port->buffer_size = encoder_output_port->buffer_size_recommended;

    if (encoder_output_port->buffer_size < encoder_output_port->buffer_size_min) {
        encoder_output_port->buffer_size = encoder_output_port->buffer_size_min;
    }

    encoder_output_port->buffer_num = encoder_output_port->buffer_num_recommended;

    if (encoder_output_port->buffer_num < encoder_output_port->buffer_num_min) {
        encoder_output_port->buffer_num = encoder_output_port->buffer_num_min;
    }


    // Commit the port changes to the output port    
    status = mmal_port_format_commit(encoder_output_port);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: unable to commit encoder output port format (%u)\n", status);
        return -1;
    }

    if (userdata->intraperiod != -1) {
        MMAL_PARAMETER_UINT32_T param = {{ MMAL_PARAMETER_INTRAPERIOD, sizeof(param)}, userdata->intraperiod};
        status = mmal_port_parameter_set(encoder_output_port, &param.hdr);
        if (status != MMAL_SUCCESS)
        {
            return -1;
        }
    }

    fprintf(stderr, " encoder input buffer_size = %d\n", encoder_input_port->buffer_size);
    fprintf(stderr, " encoder input buffer_num = %d\n", encoder_input_port->buffer_num);

    fprintf(stderr, " encoder output buffer_size = %d\n", encoder_output_port->buffer_size);
    fprintf(stderr, " encoder output buffer_num = %d\n", encoder_output_port->buffer_num);

    encoder_input_port_pool = (MMAL_POOL_T *) mmal_port_pool_create(encoder_input_port, encoder_input_port->buffer_num, encoder_input_port->buffer_size);
    userdata->encoder_input_pool = encoder_input_port_pool;
    encoder_input_port->userdata = (struct MMAL_PORT_USERDATA_T *) userdata;
    status = mmal_port_enable(encoder_input_port, encoder_input_buffer_callback);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: unable to enable encoder input port (%u)\n", status);
        return -1;
    }
    fprintf(stderr, "INFO:Encoder input pool has been created\n");


    encoder_output_port_pool = (MMAL_POOL_T *) mmal_port_pool_create(encoder_output_port, encoder_output_port->buffer_num, encoder_output_port->buffer_size);
    userdata->encoder_output_pool = encoder_output_port_pool;
    encoder_output_port->userdata = (struct MMAL_PORT_USERDATA_T *) userdata;

    status = mmal_port_enable(encoder_output_port, encoder_output_buffer_callback);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: unable to enable encoder output port (%u)\n", status);
        return -1;
    }
    fprintf(stderr, "INFO:Encoder output pool has been created\n");    

    fill_port_buffer(encoder_output_port, encoder_output_port_pool);

    fprintf(stderr, "INFO:Encoder has been created\n");
    return 0;
}

int setup_preview(PORT_USERDATA *userdata) {
    MMAL_STATUS_T status;
    MMAL_COMPONENT_T *preview = 0;
    MMAL_CONNECTION_T *camera_preview_connection = 0;
    MMAL_PORT_T *preview_input_port;

    status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_RENDERER, &preview);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: unable to create preview (%u)\n", status);
        return -1;
    }
    userdata->preview = preview;
    preview_input_port = preview->input[0];

    {
        MMAL_DISPLAYREGION_T param;
        param.hdr.id = MMAL_PARAMETER_DISPLAYREGION;
        param.hdr.size = sizeof (MMAL_DISPLAYREGION_T);
        param.set = MMAL_DISPLAY_SET_LAYER;
        param.layer = 0;
        param.set |= MMAL_DISPLAY_SET_FULLSCREEN;
        param.fullscreen = 1;
        status = mmal_port_parameter_set(preview_input_port, &param.hdr);
        if (status != MMAL_SUCCESS && status != MMAL_ENOSYS) {
            fprintf(stderr, "Error: unable to set preview port parameters (%u)\n", status);
            return -1;
        }
    }


    status = mmal_connection_create(&camera_preview_connection, userdata->camera_preview_port, preview_input_port, MMAL_CONNECTION_FLAG_TUNNELLING | MMAL_CONNECTION_FLAG_ALLOCATION_ON_INPUT);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: unable to create connection (%u)\n", status);
        return -1;
    }

    status = mmal_connection_enable(camera_preview_connection);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: unable to enable connection (%u)\n", status);
        return -1;
    }
    fprintf(stderr, "INFO: preview created\n");
    return 0;
}

static void handler(int signum) {
    pthread_exit(NULL);
}

static void *overlay_callback(void *arg) {
    PORT_USERDATA *userdata = (PORT_USERDATA *)arg;
    static sigset_t mask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);

    if (pthread_sigmask(SIG_BLOCK, &mask, NULL) != 0) {
        perror("pthread_sigmask");
        exit(1);
    }

    sqlite3 *db;
    sqlite3_stmt *stmt;
    char *zErrMsg = 0;
    int rc;

    while (userdata->sync_overlay) {
        rc = sqlite3_open(userdata->db_path, &db);
        if( rc ){
            fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
            return NULL;
        }

        rc = sqlite3_prepare_v2(db, "select * from OVERLAY limit 1", -1, &stmt, 0);
        if( rc!= SQLITE_OK) {
            fprintf(stderr, "sql error #%d: %s\n", rc, sqlite3_errmsg(db));
        } else {
            rc = sqlite3_step(stmt);
            userdata->overlay_x = sqlite3_column_int(stmt, 1);
            userdata->overlay_y = sqlite3_column_int(stmt, 2);
            userdata->overlay_text = realloc(userdata->overlay_text, strlen(sqlite3_column_text(stmt, 3))+1);
            if (userdata->overlay_text == NULL) {
                fprintf(stderr, "overly text: null");
                break;
            }
            strcpy(userdata->overlay_text, sqlite3_column_text(stmt, 3));
            userdata->overlay_py = sqlite3_column_int(stmt, 5);
            userdata->overlay_pu = sqlite3_column_int(stmt, 6);
            userdata->overlay_pv = sqlite3_column_int(stmt, 7);
        }

        sqlite3_free(zErrMsg);
        sqlite3_finalize(stmt);
        sqlite3_close(db);

        sleep(1);
    }

    if (pthread_sigmask(SIG_UNBLOCK, &mask, NULL) != 0) {
        perror("pthread_sigmask");
        exit(1);
    }

    return NULL;
}

int main(int argc, char** argv) {
    PORT_USERDATA userdata;
    MMAL_STATUS_T status;

    // Font declairation
    FT_Library ft;
    FT_Face face;
    FT_Open_Args fags;
    cairo_font_face_t* cairo_face;
    char *font_file;

    cairo_surface_t *surface,*surface2, *t_surface, *t_surface2;
    cairo_t *context,*context2, *time_ctx, *time_ctx2;

    memset(&userdata, 0, sizeof (PORT_USERDATA));

    userdata.width = VIDEO_WIDTH;
    userdata.height = VIDEO_HEIGHT;
    userdata.bitrate = VIDEO_BITRATE;
    userdata.framerate = VIDEO_FPS;
    userdata.intraperiod = VIDEO_FPS * 2;
    userdata.overlay_width = OVERLAY_WIDTH;
    userdata.overlay_font_size = OVERLAY_HEIGHT;
    userdata.overlay_height = userdata.overlay_font_size * 1.5;
    userdata.overlay_x = 0;
    userdata.overlay_y = 0;
    userdata.overlay_text = malloc(1);
    userdata.sync_overlay = 1;
    userdata.overlay_py = 223;
    userdata.overlay_pu = 56;
    userdata.overlay_pv = 184;
    userdata.db_path = "/dev/shm/web.db";
    userdata.show_display = 0;
    userdata.custom_font = 0;

    int c;
    while((c=getopt(argc, argv, "b:f:F:g:h:H:ps:w:W:")) != -1) {
        switch(c) {
            case 'b':
                userdata.bitrate = atoi(optarg);
                break;
            case 'f':
                userdata.framerate = atoi(optarg);
                break;
            case 'F':
                font_file = optarg;
                userdata.custom_font = 1;
                break;
            case 'g':
                userdata.intraperiod = atoi(optarg);
                break;
            case 'h':
                userdata.height = atoi(optarg);
                break;
            case 'H':
                userdata.overlay_font_size = atoi(optarg);
                userdata.overlay_height = userdata.overlay_font_size * 1.5;
                break;
            case 'p':
                userdata.show_display = 1;
                break;
            case 's':
                userdata.db_path = optarg;
                break;
            case 'w':
                userdata.width = atoi(optarg);
                break;
            case 'W':
                userdata.overlay_width = atoi(optarg);
                break;
        }
    }

    fprintf(stderr, "VIDEO_WIDTH       : %i\n", userdata.width);
    fprintf(stderr, "VIDEO_HEIGHT      : %i\n", userdata.height);
    fprintf(stderr, "VIDEO_FPS         : %i\n", userdata.framerate);
    fprintf(stderr, "VIDEO_BITRATE     : %i\n", userdata.bitrate);
    fprintf(stderr, "VIDEO_INTRAPERIOD : %i\n", userdata.intraperiod);
    fprintf(stderr, "OVERLAY_WIDTH     : %i\n", userdata.overlay_width);
    fprintf(stderr, "OVERLAY_HEIGHT    : %i\n", userdata.overlay_height);
    fprintf(stderr, "Running...\n");

    bcm_host_init();

    if (userdata.custom_font) {
        if (FT_Init_FreeType(&ft)) {
            fprintf(stderr, "Can't initialize freetype\n" );
            return -1;
        }

        fags.flags = FT_OPEN_PATHNAME;
        fags.pathname = font_file;

        if (FT_New_Face(ft, font_file, 0, &face ))
            fprintf( stderr, "error in open face\n");

        cairo_face = cairo_ft_font_face_create_for_ft_face(face, 0);
    }

    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, userdata.overlay_width, userdata.overlay_height);
    context = cairo_create(surface);
    cairo_rectangle(context, 0.0, 0.0, userdata.overlay_width, userdata.overlay_height);
    cairo_set_source_rgba(context, 0.0, 0.0, 0.0, 1.0);
    cairo_fill(context);
    if (userdata.custom_font)
        cairo_set_font_face(context, cairo_face);

    userdata.overlay_buffer = cairo_image_surface_get_data(surface);
    userdata.overlay = 1;

    surface2 = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, userdata.overlay_width, userdata.overlay_height);
    context2 = cairo_create(surface2);
    cairo_rectangle(context2, 0.0, 0.0, userdata.overlay_width, userdata.overlay_height);
    cairo_set_source_rgba(context2, 0.0, 0.0, 0.0, 1.0);
    cairo_fill(context2);
    if (userdata.custom_font)
        cairo_set_font_face(context2, cairo_face);

    userdata.overlay_buffer2 = cairo_image_surface_get_data(surface2);

    t_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, T_OVERLAY_WIDTH, T_OVERLAY_HEIGHT);
    time_ctx = cairo_create(t_surface);
    cairo_rectangle(time_ctx, 0.0, 0.0, T_OVERLAY_WIDTH, T_OVERLAY_HEIGHT);
    cairo_set_source_rgba(time_ctx, 0.0, 0.0, 0.0, 1.0);
    cairo_set_font_size(time_ctx, T_OVERLAY_HEIGHT);
    cairo_fill(time_ctx);

    userdata.t_overlay_buffer = cairo_image_surface_get_data(t_surface);

    t_surface2 = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, T_OVERLAY_WIDTH, T_OVERLAY_HEIGHT);
    time_ctx2 = cairo_create(t_surface2);
    cairo_rectangle(time_ctx2, 0.0, 0.0, T_OVERLAY_WIDTH, T_OVERLAY_HEIGHT);
    cairo_set_source_rgba(time_ctx2, 0.0, 0.0, 0.0, 1.0);
    cairo_set_font_size(time_ctx2, T_OVERLAY_HEIGHT);
    cairo_fill(time_ctx2);

    userdata.t_overlay_buffer2 = cairo_image_surface_get_data(t_surface2);


    if (1 && setup_camera(&userdata) != 0) {
        fprintf(stderr, "Error: setup camera %x\n", status);
        return -1;
    }

    if (1 && setup_encoder(&userdata) != 0) {
        fprintf(stderr, "Error: setup encoder %x\n", status);
        return -1;
    }


    if (userdata.show_display && setup_preview(&userdata) != 0) {
        fprintf(stderr, "Error: setup preview %x\n", status);
        return -1;
    }

    pthread_t tid;
    signal(SIGUSR1, handler);

    if (pthread_create(&tid, NULL, overlay_callback, (void *)&userdata) != 0) {
        perror("pthread_create");
        exit(1);
    }

    time_t timer;
    char text[20];
    struct tm* tm_info;

    while (1) {
        time(&timer);
        tm_info = localtime(&timer);
        strftime(text, 20, "%Y/%m/%d %H:%M:%S", tm_info);

        //Update Draw to unused buffer that way there is no flickering of the overlay text if the overlay update rate
        //and video FPS are not the same
        if (userdata.overlay == 1) {
            cairo_rectangle(context, 0.0, 0.0, userdata.overlay_width, userdata.overlay_height);
            cairo_set_source_rgba(context, 0.0, 0.0, 0.0, 1.0);
            cairo_fill(context);
            cairo_move_to(context, 0.0, 0.0);
            cairo_set_source_rgba(context, 1.0, 1.0, 1.0, 1.0);       
            cairo_move_to(context, userdata.overlay_x, userdata.overlay_font_size);
            cairo_set_font_size(context, userdata.overlay_font_size);
            cairo_show_text(context, userdata.overlay_text);

            cairo_rectangle(time_ctx, 0.0, 0.0, T_OVERLAY_WIDTH, T_OVERLAY_HEIGHT);
            cairo_set_source_rgba(time_ctx, 0.0, 0.0, 0.0, 1.0);
            cairo_fill(time_ctx);
            cairo_move_to(time_ctx, 0.0, 0.0);
            cairo_set_source_rgba(time_ctx, 1.0, 1.0, 1.0, 1.0);       
            cairo_move_to(time_ctx, 0, T_OVERLAY_HEIGHT);
            cairo_show_text(time_ctx, text);

            userdata.overlay = 0;
        }
        else {
            cairo_rectangle(context2, 0.0, 0.0, userdata.overlay_width, userdata.overlay_height);
            cairo_set_source_rgba(context2, 0.0, 0.0, 0.0, 1.0);
            cairo_fill(context2);
            cairo_move_to(context2, 0.0, 0.0);
            cairo_set_source_rgba(context2, 1.0, 1.0, 1.0, 1.0);      
            cairo_move_to(context2, userdata.overlay_x, userdata.overlay_font_size);
            cairo_set_font_size(context2, userdata.overlay_font_size);
            cairo_show_text(context2, userdata.overlay_text);

            cairo_rectangle(time_ctx2, 0.0, 0.0, T_OVERLAY_WIDTH, T_OVERLAY_HEIGHT);
            cairo_set_source_rgba(time_ctx2, 0.0, 0.0, 0.0, 1.0);
            cairo_fill(time_ctx2);
            cairo_move_to(time_ctx2, 0.0, 0.0);
            cairo_set_source_rgba(time_ctx2, 1.0, 1.0, 1.0, 1.0);       
            cairo_move_to(time_ctx2, 0, T_OVERLAY_HEIGHT);
            cairo_show_text(time_ctx2, text);

            userdata.overlay = 1;
        }

        // sleep 0.03 second to avoid cpu overloading
        usleep(30000);
    }

    userdata.sync_overlay = 0;
    pthread_kill(tid, SIGUSR1);
    pthread_join(tid, NULL);

    free(tm_info);
    free(text);

    free(userdata.overlay_text);

    cairo_destory(context);
    cairo_destory(context2);
    cairo_destory(time_ctx);
    cairo_destory(time_ctx2);
    cairo_surface_destroy(t_surface);
    cairo_surface_destroy(t_surface2);
    cairo_surface_destroy(surface);
    cairo_surface_destroy(surface2);

    free(font_file);
    cairo_font_face_destroy(cairo_face);
    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    return 0;
}