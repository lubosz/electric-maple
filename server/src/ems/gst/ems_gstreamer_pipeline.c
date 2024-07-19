// Copyright 2019-2024, Collabora, Ltd.
// Copyright 2023, Pluto VR, Inc.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  A GStreamer pipeline for WebRTC streaming
 * @author Moshi Turner <moses@collabora.com>
 * @author Jakub Adam <jakub.adam@collabora.com
 * @author Nicolas Dufresne <nicolas.dufresne@collabora.com>
 * @author Olivier Crête <olivier.crete@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Aaron Boxer <aaron.boxer@collabora.com>
 * @author Rylie Pavlik <rpavlik@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup aux_util
 */

#include "ems_gstreamer_pipeline.h"

#include "ems_callbacks.h"

#include "os/os_threading.h"
#include "util/u_misc.h"

#include "electricmaple.pb.h"

#include <pb_decode.h>
#include <pb_encode.h>

#include "ems_signaling_server.h"

#include <gst/gst.h>
#include <gst/gststructure.h>
#include <gst/rtp/gstrtpbuffer.h>

#define GST_USE_UNSTABLE_API
#include <gst/webrtc/datachannel.h>
#include <gst/webrtc/rtcsessiondescription.h>
#undef GST_USE_UNSTABLE_API

#include <stdio.h>
#include <assert.h>

#include "ems_pipeline_args.h"

#define WEBRTC_TEE_NAME "webrtctee"

#ifdef __aarch64__
#define DEFAULT_VIDEOSINK " queue max-size-bytes=0 ! kmssink bus-id=a0070000.v_mix"
#else
#define DEFAULT_VIDEOSINK " videoconvert ! autovideosink "
#endif

// TODO: Can we define the below at a higher level so it can also be
//       picked-up by em_stream_client ?
#define RTP_TWOBYTES_HDR_EXT_ID 1 // Must be in the [1,15] range
#define RTP_TWOBYTES_HDR_EXT_MAX_SIZE 255


EmsSignalingServer *signaling_server;


struct ems_gstreamer_pipeline
{
	struct gstreamer_pipeline base;

	GstWebRTCDataChannel *data_channel;
	guint timeout_src_id;

	struct ems_callbacks *callbacks;

	bool have_ever_sent_a_down_msg;
	struct timespec last_print_time;
	GSList *sent_down_msg_list;
};

static gboolean
gst_bus_cb(GstBus *bus, GstMessage *message, gpointer user_data)
{
	struct ems_gstreamer_pipeline *egp = (struct ems_gstreamer_pipeline *)user_data;
	GstBin *pipeline = GST_BIN(egp->base.pipeline);

	switch (GST_MESSAGE_TYPE(message)) {
	case GST_MESSAGE_ERROR: {
		GError *gerr;
		gchar *debug_msg;
		gst_message_parse_error(message, &gerr, &debug_msg);
		GST_DEBUG_BIN_TO_DOT_FILE(pipeline, GST_DEBUG_GRAPH_SHOW_ALL, "mss-pipeline-ERROR");
		U_LOG_E("Error: %s (%s)", gerr->message, debug_msg);

		// Don't try streaming when pipeline doesn't start correctly.
		if (gerr->domain == GST_STREAM_ERROR) {
			if (gerr->code == GST_STREAM_ERROR_FAILED) {
				U_LOG_E("GStreamer encountered a fatal error");
				// TODO: Shutdown gracefully
				exit(EXIT_FAILURE);
			}
		}

		g_error_free(gerr);
		g_free(debug_msg);
	} break;
	case GST_MESSAGE_WARNING: {
		GError *gerr;
		gchar *debug_msg;
		gst_message_parse_warning(message, &gerr, &debug_msg);
		GST_DEBUG_BIN_TO_DOT_FILE(pipeline, GST_DEBUG_GRAPH_SHOW_ALL, "mss-pipeline-WARNING");
		U_LOG_W("Warning: %s (%s)", gerr->message, debug_msg);
		g_error_free(gerr);
		g_free(debug_msg);
	} break;
	case GST_MESSAGE_EOS: {
		g_error("Got EOS!!");
	} break;
	default: break;
	}
	return TRUE;
}

static GstElement *
get_webrtcbin_for_client(GstBin *pipeline, EmsClientId client_id)
{
	gchar *name;
	GstElement *webrtcbin;

	name = g_strdup_printf("webrtcbin_%p", client_id);
	webrtcbin = gst_bin_get_by_name(pipeline, name);
	g_free(name);

	return webrtcbin;
}

static void
connect_webrtc_to_tee(GstElement *webrtcbin)
{
	GstElement *pipeline;
	GstElement *tee;
	GstPad *srcpad;
	GstPad *sinkpad;
	GstPadLinkReturn ret;

	pipeline = GST_ELEMENT(gst_element_get_parent(webrtcbin));
	if (pipeline == NULL)
		return;
	tee = gst_bin_get_by_name(GST_BIN(pipeline), WEBRTC_TEE_NAME);
	srcpad = gst_element_request_pad_simple(tee, "src_%u");
	sinkpad = gst_element_request_pad_simple(webrtcbin, "sink_0");
	ret = gst_pad_link(srcpad, sinkpad);
	g_assert(ret == GST_PAD_LINK_OK);

	GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-on-offer");

	gst_object_unref(srcpad);
	gst_object_unref(sinkpad);
	gst_object_unref(tee);
	gst_object_unref(pipeline);
}

static void
on_offer_created(GstPromise *promise, GstElement *webrtcbin)
{
	GstWebRTCSessionDescription *offer = NULL;
	gchar *sdp;

	gst_structure_get(gst_promise_get_reply(promise), "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
	gst_promise_unref(promise);

	g_signal_emit_by_name(webrtcbin, "set-local-description", offer, NULL);

	sdp = gst_sdp_message_as_text(offer->sdp);
	ems_signaling_server_send_sdp_offer(signaling_server, g_object_get_data(G_OBJECT(webrtcbin), "client_id"), sdp);
	g_free(sdp);

	gst_webrtc_session_description_free(offer);

	connect_webrtc_to_tee(webrtcbin);
}

static void
webrtc_on_data_channel_cb(GstElement *webrtcbin, GObject *data_channel, struct ems_gstreamer_pipeline *egp)
{
	U_LOG_I("webrtc_on_data_channel_cb called");
}



static void
webrtc_on_ice_candidate_cb(GstElement *webrtcbin, guint mlineindex, gchar *candidate)
{
	ems_signaling_server_send_candidate(signaling_server, g_object_get_data(G_OBJECT(webrtcbin), "client_id"),
	                                    mlineindex, candidate);
}


static void
data_channel_error_cb(GstWebRTCDataChannel *datachannel, struct ems_gstreamer_pipeline *egp)
{
	U_LOG_E("error");
}

gboolean
datachannel_send_message(GstWebRTCDataChannel *datachannel)
{
	g_signal_emit_by_name(datachannel, "send-string", "Hi! from Electric Maple Server");

	char buf[] = "Electric Maple Server";
	GBytes *b = g_bytes_new_static(buf, ARRAY_SIZE(buf));
	gst_webrtc_data_channel_send_data(datachannel, b);

	return G_SOURCE_CONTINUE;
}

static void
data_channel_open_cb(GstWebRTCDataChannel *datachannel, struct ems_gstreamer_pipeline *egp)
{
	U_LOG_I("data channel opened");

	egp->timeout_src_id = g_timeout_add_seconds(3, G_SOURCE_FUNC(datachannel_send_message), datachannel);
}

static void
data_channel_close_cb(GstWebRTCDataChannel *datachannel, struct ems_gstreamer_pipeline *egp)
{
	U_LOG_I("data channel closed");

	g_clear_handle_id(&egp->timeout_src_id, g_source_remove);
	g_clear_object(&egp->data_channel);
}

static void
data_channel_message_data_cb(GstWebRTCDataChannel *datachannel, GBytes *data, struct ems_gstreamer_pipeline *egp)
{
	em_proto_UpMessage message = em_proto_UpMessage_init_default;
	size_t n = 0;

	const unsigned char *buf = (const unsigned char *)g_bytes_get_data(data, &n);
	pb_istream_t our_istream = pb_istream_from_buffer(buf, n);

	bool result = pb_decode_ex(&our_istream, &em_proto_UpMessage_msg, &message, PB_DECODE_NULLTERMINATED);

	if (!result) {
		U_LOG_E("Error! %s", PB_GET_ERROR(&our_istream));
		return;
	}
	ems_callbacks_call(egp->callbacks, EMS_CALLBACKS_EVENT_TRACKING, &message);
}

static void
data_channel_message_string_cb(GstWebRTCDataChannel *datachannel, gchar *str, struct ems_gstreamer_pipeline *egp)
{
	// U_LOG_I("Received data channel message: %s", str);
}

static int
compare_int_ascending(gconstpointer a, gconstpointer b)
{
	return GPOINTER_TO_INT(a) - GPOINTER_TO_INT(b);
}

static void
benchmark_down_msg_loss(struct ems_gstreamer_pipeline *self, GstMapInfo *map_info)
{
	// DownMessages are not in order here!
	pb_istream_t our_istream = pb_istream_from_buffer(map_info->data, map_info->size);
	em_proto_DownMessage msg;
	bool result = pb_decode_ex(&our_istream, em_proto_DownMessage_fields, &msg, PB_DECODE_NULLTERMINATED);
	if (!result) {
		U_LOG_E("Decoding protobuf failed: %s downMsg_bytes size: %ld", PB_GET_ERROR(&our_istream),
		        map_info->size);
	} else {

		if (!self->have_ever_sent_a_down_msg) {
			clock_gettime(CLOCK_MONOTONIC, &self->last_print_time);
			self->sent_down_msg_list = NULL;
			self->have_ever_sent_a_down_msg = true;
		}

		self->sent_down_msg_list =
		    g_slist_prepend(self->sent_down_msg_list, GINT_TO_POINTER((gint)msg.frame_data.frame_sequence_id));

		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);

		double duration_since_last_print_secs;
		duration_since_last_print_secs = (double)(now.tv_sec - self->last_print_time.tv_sec);
		duration_since_last_print_secs += (double)(now.tv_nsec - self->last_print_time.tv_nsec) / (double)1e9;

		if (duration_since_last_print_secs >= 5.0) {

			self->sent_down_msg_list = g_slist_sort(self->sent_down_msg_list, compare_int_ascending);

			GSList *l = self->sent_down_msg_list;
			gint last = -1;
			gint down_msg_loss_accumulator = 0;
			while (l != NULL) {
				int current = GPOINTER_TO_INT(l->data);
				if (last != -1) {
					gint num_skipped = (current - last) - 1;
					if (num_skipped > 0) {
						// U_LOG_D("Skipped payloading %d DownMsgs (%d - %d)", num_skipped, last
						// + 1, current - 1);
						down_msg_loss_accumulator += num_skipped;
					}
				}
				last = current;
				l = l->next;
			}

			double skip_per_second = (double)down_msg_loss_accumulator / duration_since_last_print_secs;
			U_LOG_D("Skipping DownMsgs at rate %.2f/second", skip_per_second);
			clock_gettime(CLOCK_MONOTONIC, &self->last_print_time);

			g_slist_free(self->sent_down_msg_list);
			self->sent_down_msg_list = NULL;
		}

		// U_LOG_I("Adding DownMsg for Frame #%ld to RTP buffer.", msg.frame_data.frame_sequence_id);
	}
}

GstPadProbeReturn
rtppay_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
	(void)pad;

	GstBuffer *buffer;
	GstRTPBuffer rtp_buffer = GST_RTP_BUFFER_INIT;
	struct ems_gstreamer_pipeline *self = user_data;

	buffer = gst_pad_probe_info_get_buffer(info);

	buffer = gst_buffer_make_writable(buffer);

	if (!gst_rtp_buffer_map(buffer, GST_MAP_WRITE, &rtp_buffer)) {
		U_LOG_E("Failed to map GstBuffer");
		// be more fault tolerant!
		return GST_PAD_PROBE_OK;
	}

	// Inject extension data
	GstCustomMeta *custom_meta = gst_buffer_get_custom_meta(buffer, "down-message");
	if (!custom_meta) {
		gst_rtp_buffer_unmap(&rtp_buffer);
		// U_LOG_W("Failed to get custom meta from GstBuffer!");
		return GST_PAD_PROBE_OK;
	}

	GstStructure *custom_structure = gst_custom_meta_get_structure(custom_meta);

	GstBuffer *struct_buf;
	if (!gst_structure_get(custom_structure, "protobuf", GST_TYPE_BUFFER, &struct_buf, NULL)) {
		U_LOG_E("Could not read protobuf from struct");
		return GST_PAD_PROBE_OK;
	}

	GstMapInfo map_info;
	if (!gst_buffer_map(struct_buf, &map_info, GST_MAP_READ)) {
		U_LOG_E("Failed to map custom meta buffer.");
		return GST_PAD_PROBE_OK;
	}

	if (map_info.size > RTP_TWOBYTES_HDR_EXT_MAX_SIZE) {
		U_LOG_E("Data in too large for RTP header (%ld > %d bytes). Implement multi-extension-element support.",
		        map_info.size, RTP_TWOBYTES_HDR_EXT_MAX_SIZE);
		gst_rtp_buffer_unmap(&rtp_buffer);
		return GST_PAD_PROBE_OK;
	}

	if (!gst_rtp_buffer_add_extension_twobytes_header(&rtp_buffer, 0 /* appbits */, RTP_TWOBYTES_HDR_EXT_ID,
	                                                  map_info.data, (guint)map_info.size)) {
		U_LOG_E("Failed to add extension data !");
		return GST_PAD_PROBE_OK;
	}

	// The bit should be written by gst_rtp_buffer_add_extension_twobytes_header
	if (!gst_rtp_buffer_get_extension(&rtp_buffer)) {
		U_LOG_E("The RTP extension bit was not set.");
	}

	if (ems_arguments_get()->benchmark_down_msg) {
		benchmark_down_msg_loss(self, &map_info);
	}

	gst_rtp_buffer_unmap(&rtp_buffer);
	gst_buffer_unmap(struct_buf, &map_info);

	return GST_PAD_PROBE_OK;
}

static bool
ems_gstreamer_pipeline_add_payload_pad_probe(struct ems_gstreamer_pipeline *self, GstElement *webrtcbin)
{
	GstPipeline *pipeline = GST_PIPELINE(self->base.pipeline);

	GstElement *rtppay = gst_bin_get_by_name(GST_BIN(pipeline), "rtppay");
	if (rtppay == NULL) {
		U_LOG_E("Could not find rtppay element.");
		return false;
	}

	GstPad *pad = gst_element_get_static_pad(rtppay, "src");
	if (pad == NULL) {
		U_LOG_E("Could not find static src pad in rtppay.");
		return false;
	}

	self->have_ever_sent_a_down_msg = false;

	gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, rtppay_probe, self, NULL);
	gst_object_unref(pad);

	return true;
}


static void
webrtc_client_connected_cb(EmsSignalingServer *server, EmsClientId client_id, struct ems_gstreamer_pipeline *egp)
{
	GstBin *pipeline = GST_BIN(egp->base.pipeline);
	gchar *name;
	GstElement *webrtcbin;
	GstCaps *caps;
	GstStateChangeReturn ret;
	GstWebRTCRTPTransceiver *transceiver;

	name = g_strdup_printf("webrtcbin_%p", client_id);

	webrtcbin = gst_element_factory_make("webrtcbin", name);
	g_object_set(webrtcbin, "bundle-policy", GST_WEBRTC_BUNDLE_POLICY_MAX_BUNDLE, NULL);
	g_object_set_data(G_OBJECT(webrtcbin), "client_id", client_id);
	gst_bin_add(pipeline, webrtcbin);

	ret = gst_element_set_state(webrtcbin, GST_STATE_READY);
	g_assert(ret != GST_STATE_CHANGE_FAILURE);

	g_signal_connect(webrtcbin, "on-data-channel", G_CALLBACK(webrtc_on_data_channel_cb), NULL);

	// I also think this would work if the pipeline state is READY but /shrug

	// TODO add priority
	GstStructure *data_channel_options = gst_structure_new_from_string("data-channel-options, ordered=true");
	g_signal_emit_by_name(webrtcbin, "create-data-channel", "channel", data_channel_options, &egp->data_channel);
	gst_clear_structure(&data_channel_options);

	if (!egp->data_channel) {
		U_LOG_E("Couldn't make datachannel!");
		assert(false);
	} else {
		U_LOG_I("Successfully created datachannel!");

		g_signal_connect(egp->data_channel, "on-open", G_CALLBACK(data_channel_open_cb), egp);
		g_signal_connect(egp->data_channel, "on-close", G_CALLBACK(data_channel_close_cb), egp);
		g_signal_connect(egp->data_channel, "on-error", G_CALLBACK(data_channel_error_cb), egp);
		g_signal_connect(egp->data_channel, "on-message-data", G_CALLBACK(data_channel_message_data_cb), egp);
		g_signal_connect(egp->data_channel, "on-message-string", G_CALLBACK(data_channel_message_string_cb),
		                 egp);
	}

	ret = gst_element_set_state(webrtcbin, GST_STATE_PLAYING);
	g_assert(ret != GST_STATE_CHANGE_FAILURE);

	g_signal_connect(webrtcbin, "on-ice-candidate", G_CALLBACK(webrtc_on_ice_candidate_cb), NULL);

	caps = gst_caps_from_string(
	    "application/x-rtp, "
	    "payload=96,encoding-name=H264,clock-rate=90000,media=video,packetization-mode=(string)1,profile-level-id=("
	    "string)42e01f");
	g_signal_emit_by_name(webrtcbin, "add-transceiver", GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY, caps,
	                      &transceiver);

	gst_caps_unref(caps);
	gst_clear_object(&transceiver);

	g_signal_emit_by_name(
	    webrtcbin, "create-offer", NULL,
	    gst_promise_new_with_change_func((GstPromiseChangeFunc)on_offer_created, webrtcbin, NULL));

	GST_DEBUG_BIN_TO_DOT_FILE(pipeline, GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-client-connected");

	if (!ems_gstreamer_pipeline_add_payload_pad_probe(egp, webrtcbin)) {
		U_LOG_E("Failed to add payload pad probe.");
	}

	g_free(name);
}

static void
webrtc_sdp_answer_cb(EmsSignalingServer *server,
                     EmsClientId client_id,
                     const gchar *sdp,
                     struct ems_gstreamer_pipeline *egp)
{
	GstBin *pipeline = GST_BIN(egp->base.pipeline);
	GstSDPMessage *sdp_msg = NULL;
	GstWebRTCSessionDescription *desc = NULL;

	if (gst_sdp_message_new_from_text(sdp, &sdp_msg) != GST_SDP_OK) {
		g_debug("Error parsing SDP description");
		goto out;
	}

	desc = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_ANSWER, sdp_msg);
	if (desc) {
		GstElement *webrtcbin;
		GstPromise *promise;

		webrtcbin = get_webrtcbin_for_client(pipeline, client_id);
		if (!webrtcbin) {
			goto out;
		}
		promise = gst_promise_new();

		g_signal_emit_by_name(webrtcbin, "set-remote-description", desc, promise);

		gst_promise_wait(promise);
		gst_promise_unref(promise);

		gst_object_unref(webrtcbin);
	} else {
		gst_sdp_message_free(sdp_msg);
	}

out:
	g_clear_pointer(&desc, gst_webrtc_session_description_free);
}

static void
webrtc_candidate_cb(EmsSignalingServer *server,
                    EmsClientId client_id,
                    guint mlineindex,
                    const gchar *candidate,
                    struct ems_gstreamer_pipeline *egp)
{
	GstBin *pipeline = GST_BIN(egp->base.pipeline);

	if (strlen(candidate)) {
		GstElement *webrtcbin;

		webrtcbin = get_webrtcbin_for_client(pipeline, client_id);
		if (webrtcbin) {
			g_signal_emit_by_name(webrtcbin, "add-ice-candidate", mlineindex, candidate);
			gst_object_unref(webrtcbin);
		}
	}

	g_debug("Remote candidate: %s", candidate);
}

static GstPadProbeReturn
remove_webrtcbin_probe_cb(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
	GstElement *webrtcbin = GST_ELEMENT(user_data);

	gst_bin_remove(GST_BIN(GST_ELEMENT_PARENT(webrtcbin)), webrtcbin);
	gst_element_set_state(webrtcbin, GST_STATE_NULL);

	return GST_PAD_PROBE_REMOVE;
}

static void
webrtc_client_disconnected_cb(EmsSignalingServer *server, EmsClientId client_id, struct ems_gstreamer_pipeline *egp)
{
	GstBin *pipeline = GST_BIN(egp->base.pipeline);
	GstElement *webrtcbin;

	webrtcbin = get_webrtcbin_for_client(pipeline, client_id);

	if (webrtcbin) {
		GstPad *sinkpad;

		sinkpad = gst_element_get_static_pad(webrtcbin, "sink_0");
		if (sinkpad) {
			gst_pad_add_probe(GST_PAD_PEER(sinkpad), GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
			                  remove_webrtcbin_probe_cb, webrtcbin, gst_object_unref);

			gst_clear_object(&sinkpad);
		}
	}
}

/*
 *
 * Internal pipeline functions.
 *
 */

static void
break_apart(struct xrt_frame_node *node)
{
	struct gstreamer_pipeline *gp = container_of(node, struct gstreamer_pipeline, node);

	/*
	 * This function is called when we are shutting down, after returning
	 * from this function you are not allowed to call any other nodes in the
	 * graph. But it must be safe for other nodes to call any normal
	 * functions on us. Once the context is done calling break_aprt on all
	 * objects it will call destroy on them.
	 */

	(void)gp;
}

static void
destroy(struct xrt_frame_node *node)
{
	struct gstreamer_pipeline *gp = container_of(node, struct gstreamer_pipeline, node);

	/*
	 * All of the nodes has been broken apart and none of our functions will
	 * be called, it's now safe to destroy and free ourselves.
	 */

	U_LOG_I("Shutting down em pipeline.");

	free(gp);
}

GMainLoop *main_loop;

void *
loop_thread(void *data)
{
	g_main_loop_run(main_loop);
	return NULL;
}


/*
 *
 * Exported functions.
 *
 */
GBytes *
ems_gstreamer_pipeline_encode_down_msg(em_proto_DownMessage *msg)
{

	uint8_t buf[em_proto_DownMessage_size];
	pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));

	if (!pb_encode(&os, em_proto_DownMessage_fields, msg)) {
		U_LOG_E("Failed to encode protobuf.");
		return NULL;
	}

	return g_bytes_new(buf, os.bytes_written);
}

void
ems_gstreamer_pipeline_play(struct gstreamer_pipeline *gp)
{
	U_LOG_I("Starting pipeline");
	struct ems_gstreamer_pipeline *egp = (struct ems_gstreamer_pipeline *)gp;

	main_loop = g_main_loop_new(NULL, FALSE);


	GstStateChangeReturn ret = gst_element_set_state(egp->base.pipeline, GST_STATE_PLAYING);

	g_assert(ret != GST_STATE_CHANGE_FAILURE);

	g_signal_connect(signaling_server, "ws-client-connected", G_CALLBACK(webrtc_client_connected_cb), egp);

	pthread_t thread;
	pthread_create(&thread, NULL, loop_thread, NULL);
}

void
ems_gstreamer_pipeline_stop_if_playing(struct gstreamer_pipeline *gp)
{
	struct ems_gstreamer_pipeline *egp = (struct ems_gstreamer_pipeline *)gp;

	GstState state;
	GstState pending;

	GstStateChangeReturn ret = gst_element_get_state(egp->base.pipeline, &state, &pending, 3 * GST_SECOND);
	if (ret != GST_STATE_CHANGE_SUCCESS) {
		U_LOG_E("Unable to get pipeline state.");
		return;
	}

	if (state == GST_STATE_PLAYING) {
		ems_gstreamer_pipeline_stop(gp);
	}
}

void
ems_gstreamer_pipeline_stop(struct gstreamer_pipeline *gp)
{
	struct ems_gstreamer_pipeline *egp = (struct ems_gstreamer_pipeline *)gp;
	U_LOG_I("Stopping pipeline");

	// Settle the pipeline.
	U_LOG_T("Sending EOS");
	gst_element_send_event(egp->base.pipeline, gst_event_new_eos());

	// Wait for EOS message on the pipeline bus.
	U_LOG_T("Waiting for EOS");
	GstMessage *msg = NULL;
	msg = gst_bus_timed_pop_filtered(GST_ELEMENT_BUS(egp->base.pipeline), 3 * GST_SECOND,
	                                 GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
	//! @todo Should check if we got an error message here or an eos.
	(void)msg;

	// Completely stop the pipeline.
	U_LOG_T("Setting to NULL");
	gst_element_set_state(egp->base.pipeline, GST_STATE_NULL);
}



void
ems_gstreamer_pipeline_create(struct xrt_frame_context *xfctx,
                              const char *appsrc_name,
                              struct ems_callbacks *callbacks_collection,
                              struct gstreamer_pipeline **out_gp)
{
	gchar *pipeline_str;
	GstElement *pipeline;
	GError *error = NULL;
	GstBus *bus;

	signaling_server = ems_signaling_server_new();

	struct ems_arguments *args = ems_arguments_get();

	gchar *debug_file_path = NULL;
	if (args->stream_debug_file) {
		debug_file_path = g_file_get_path(args->stream_debug_file);
	}

	gchar *save_tee_str = NULL;
	if (debug_file_path) {
		save_tee_str = g_strdup_printf(
		    "tee name=savetee "
		    "savetee. ! queue ! matroskamux ! filesink location=%s "
		    "savetee. ! ",
		    debug_file_path);
	} else {
		save_tee_str = g_strdup("");
	}

	gchar *encoder_str = NULL;
	if (args->encoder_type == EMS_ENCODER_TYPE_X264) {
		encoder_str = g_strdup_printf(
		    "videoconvert ! "
		    "video/x-raw,format=NV12 ! "
		    "queue ! "
		    "x264enc tune=zerolatency sliced-threads=true speed-preset=veryfast bframes=2 bitrate=%d",
		    args->bitrate);
	} else if (args->encoder_type == EMS_ENCODER_TYPE_NVH264) {
		const char *nvenc_pipe =
		    "videoconvert ! queue !"
		    "nvh264enc zerolatency=true bitrate=%d rc-mode=cbr preset=low-latency";
		encoder_str = g_strdup_printf(nvenc_pipe, args->bitrate);
	} else if (args->encoder_type == EMS_ENCODER_TYPE_NVAUTOGPUH264) {
		const char *nvenc_pipe =
		    "cudaupload ! queue ! cudaconvert ! "
		    "nvautogpuh264enc bitrate=%d rate-control=cbr preset=p1 tune=low-latency "
		    "multi-pass=two-pass-quarter zero-reorder-delay=true cc-insert=disabled cabac=false";
		encoder_str = g_strdup_printf(nvenc_pipe, args->bitrate);
	} else if (args->encoder_type == EMS_ENCODER_TYPE_VULKANH264) {
		// TODO: Make vulkancolorconvert work with vulkanh264enc
		encoder_str = g_strdup_printf(
		    "videoconvert ! "
		    "video/x-raw,format=NV12 ! "
		    "queue ! "
		    "vulkanupload ! vulkanh264enc average-bitrate=%d ! h264parse",
		    args->bitrate);
	} else if (args->encoder_type == EMS_ENCODER_TYPE_OPENH264) {
		encoder_str = g_strdup_printf(
		    "videoconvert ! "
		    "video/x-raw,format=I420 ! "
		    "queue ! "
		    "openh264enc complexity=high rate-control=quality bitrate=%d",
		    args->bitrate);
	} else {
		U_LOG_E("Unexpected encoder type.");
		abort();
	}

	pipeline_str = g_strdup_printf(
	    "appsrc name=%s ! "            //
	    "%s ! "                        //
	    "video/x-h264,profile=main ! " //
	    "%s"
	    "queue ! "                                    //
	    "rtph264pay name=rtppay config-interval=1 ! " //
	    "application/x-rtp,payload=96 ! "             //
	    "tee name=%s allow-not-linked=true",
	    appsrc_name, encoder_str, save_tee_str, WEBRTC_TEE_NAME);

	g_free(debug_file_path);
	g_free(save_tee_str);
	g_free(encoder_str);

	// no webrtc bin yet until later!

	printf("%s\n\n\n\n", pipeline_str);

	struct ems_gstreamer_pipeline *egp = U_TYPED_CALLOC(struct ems_gstreamer_pipeline);
	egp->base.node.break_apart = break_apart;
	egp->base.node.destroy = destroy;
	egp->base.xfctx = xfctx;
	egp->callbacks = callbacks_collection;

	gst_init(NULL, NULL);

	pipeline = gst_parse_launch(pipeline_str, &error);
	g_assert_no_error(error);
	g_free(pipeline_str);

	bus = gst_element_get_bus(pipeline);
	gst_bus_add_watch(bus, gst_bus_cb, egp);
	gst_object_unref(bus);

	g_signal_connect(signaling_server, "ws-client-disconnected", G_CALLBACK(webrtc_client_disconnected_cb), egp);
	g_signal_connect(signaling_server, "sdp-answer", G_CALLBACK(webrtc_sdp_answer_cb), egp);
	g_signal_connect(signaling_server, "candidate", G_CALLBACK(webrtc_candidate_cb), egp);

	g_print(
	    "Output streams:\n"
	    "\tWebRTC: http://127.0.0.1:8080\n");

	// Setup pipeline.
	egp->base.pipeline = pipeline;
	// GstElement *appsrc = gst_element_factory_make("appsrc", appsrc_name);
	// GstElement *conv = gst_element_factory_make("videoconvert", "conv");
	// GstElement *scale = gst_element_factory_make("videoscale", "scale");
	// GstElement *videosink = gst_element_factory_make("autovideosink", "videosink");


	/*
	 * Add ourselves to the context so we are destroyed.
	 * This is done once we know everything is completed.
	 */
	xrt_frame_context_add(xfctx, &egp->base.node);

	*out_gp = &egp->base;
}
