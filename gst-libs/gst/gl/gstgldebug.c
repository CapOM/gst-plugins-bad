/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gl.h"
#include "gstgldebug.h"
#include <glib/gprintf.h>
#include <string.h>

#define ASYNC_DEBUG_FILLED (1 << 0)
#define ASYNC_DEBUG_FROZEN (1 << 1)

/* compatibility defines */
#ifndef GL_DEBUG_TYPE_ERROR
#define GL_DEBUG_TYPE_ERROR 0x824C
#endif
#ifndef GL_DEBUG_TYPE_DEPRECATED_BEHAVIOUR
#define GL_DEBUG_TYPE_DEPRECATED_BEHAVIOUR 0x824D
#endif
#ifndef GL_DEBUG_TYPE_UNDEFINED_BEHAVIOUR
#define GL_DEBUG_TYPE_UNDEFINED_BEHAVIOUR 0x824E
#endif
#ifndef GL_DEBUG_TYPE_PORTABILITY
#define GL_DEBUG_TYPE_PORTABILITY 0x824F
#endif
#ifndef GL_DEBUG_TYPE_PERFORMANCE
#define GL_DEBUG_TYPE_PERFORMANCE 0x8250
#endif
#ifndef GL_DEBUG_TYPE_MARKER
#define GL_DEBUG_TYPE_MARKER 0x8268
#endif
#ifndef GL_DEBUG_TYPE_OTHER
#define GL_DEBUG_TYPE_OTHER 0x8251
#endif

#ifndef GL_DEBUG_SEVERITY_HIGH
#define GL_DEBUG_SEVERITY_HIGH 0x9146
#endif
#ifndef GL_DEBUG_SEVERITY_MEDIUM
#define GL_DEBUG_SEVERITY_MEDIUM 0x9147
#endif
#ifndef GL_DEBUG_SEVERITY_LOW
#define GL_DEBUG_SEVERITY_LOW 0x9148
#endif
#ifndef GL_DEBUG_SEVERITY_NOTIFICATION
#define GL_DEBUG_SEVERITY_NOTIFICATION 0x826B
#endif

#ifndef GL_DEBUG_SOURCE_API
#define GL_DEBUG_SOURCE_API 0x8246
#endif
#ifndef GL_DEBUG_SOURCE_WINDOW_SYSTEM
#define GL_DEBUG_SOURCE_WINDOW_SYSTEM 0x8247
#endif
#ifndef GL_DEBUG_SOURCE_SHADER_COMPILER
#define GL_DEBUG_SOURCE_SHADER_COMPILER 0x8248
#endif
#ifndef GL_DEBUG_SOURCE_THIRD_PARTY
#define GL_DEBUG_SOURCE_THIRD_PARTY 0x8249
#endif
#ifndef GL_DEBUG_SOURCE_APPLICATION
#define GL_DEBUG_SOURCE_APPLICATION 0x824A
#endif
#ifndef GL_DEBUG_SOURCE_OTHER
#define GL_DEBUG_SOURCE_OTHER 0x824B
#endif

GST_DEBUG_CATEGORY_STATIC (gst_performance);
#define GST_CAT_DEFAULT gst_gl_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);
GST_DEBUG_CATEGORY_STATIC (default_debug);

static void
_init_debug (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_GET (gst_performance, "GST_PERFORMANCE");
    GST_DEBUG_CATEGORY_INIT (gst_gl_debug, "gldebug", 0, "OpenGL Debugging");
    GST_DEBUG_CATEGORY_GET (default_debug, "default");
    g_once_init_leave (&_init, 1);
  }
}

static void
_free_async_debug_data (GstGLAsyncDebug * ad)
{
  if (ad->debug_msg) {
    g_free (ad->debug_msg);
    ad->debug_msg = NULL;
    if (ad->object)
      g_object_unref (ad->object);
    ad->object = NULL;
    ad->state_flags &= ~ASYNC_DEBUG_FILLED;
  }
}

void
gst_gl_async_debug_init (GstGLAsyncDebug * ad)
{
  _init_debug ();

  memset (ad, 0, sizeof (*ad));
}

void
gst_gl_async_debug_unset (GstGLAsyncDebug * ad)
{
  gst_gl_async_debug_output_log_msg (ad);

  _free_async_debug_data (ad);

  if (ad->notify)
    ad->notify (ad->user_data);
}

GstGLAsyncDebug *
gst_gl_async_debug_new (void)
{
  return g_new0 (GstGLAsyncDebug, 1);
}

void
gst_gl_async_debug_free (GstGLAsyncDebug * ad)
{
  gst_gl_async_debug_unset (ad);
  g_free (ad);
}

/**
 * gst_gl_async_debug_freeze:
 * @ad: a #GstGLAsyncDebug
 *
 * freeze the debug output.  While frozen, any call to
 * gst_gl_async_debug_output_log_msg() will not output any messages but
 * subsequent calls to gst_gl_async_debug_store_log_msg() will overwrite previous
 * messages.
 */
void
gst_gl_async_debug_freeze (GstGLAsyncDebug * ad)
{
  ad->state_flags |= ASYNC_DEBUG_FROZEN;
}

/**
 * gst_gl_async_debug_thaw:
 * @ad: a #GstGLAsyncDebug
 *
 * unfreeze the debug output.  See gst_gl_async_debug_freeze() for what freezing means
 */
void
gst_gl_async_debug_thaw (GstGLAsyncDebug * ad)
{
  ad->state_flags &= ~ASYNC_DEBUG_FROZEN;
}

#if !defined(GST_DISABLE_GST_DEBUG)

static inline const gchar *
_debug_severity_to_string (GLenum severity)
{
  switch (severity) {
    case GL_DEBUG_SEVERITY_HIGH:
      return "high";
    case GL_DEBUG_SEVERITY_MEDIUM:
      return "medium";
    case GL_DEBUG_SEVERITY_LOW:
      return "low";
    case GL_DEBUG_SEVERITY_NOTIFICATION:
      return "notification";
    default:
      return "invalid";
  }
}

static inline const gchar *
_debug_source_to_string (GLenum source)
{
  switch (source) {
    case GL_DEBUG_SOURCE_API:
      return "API";
    case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
      return "winsys";
    case GL_DEBUG_SOURCE_SHADER_COMPILER:
      return "shader compiler";
    case GL_DEBUG_SOURCE_THIRD_PARTY:
      return "third party";
    case GL_DEBUG_SOURCE_APPLICATION:
      return "application";
    case GL_DEBUG_SOURCE_OTHER:
      return "other";
    default:
      return "invalid";
  }
}

static inline const gchar *
_debug_type_to_string (GLenum type)
{
  switch (type) {
    case GL_DEBUG_TYPE_ERROR:
      return "error";
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOUR:
      return "deprecated";
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOUR:
      return "undefined";
    case GL_DEBUG_TYPE_PORTABILITY:
      return "portability";
    case GL_DEBUG_TYPE_PERFORMANCE:
      return "performance";
    case GL_DEBUG_TYPE_MARKER:
      return "debug marker";
    case GL_DEBUG_TYPE_OTHER:
      return "other";
    default:
      return "invalid";
  }
}

/* silence the compiler... */
void GSTGLAPI _gst_gl_debug_callback (GLenum source, GLenum type, GLuint id,
    GLenum severity, GLsizei length, const gchar * message, gpointer user_data);

void GSTGLAPI
_gst_gl_debug_callback (GLenum source, GLenum type, GLuint id, GLenum severity,
    GLsizei length, const gchar * message, gpointer user_data)
{
  GstGLContext *context = user_data;
  const gchar *severity_str = _debug_severity_to_string (severity);
  const gchar *source_str = _debug_source_to_string (source);
  const gchar *type_str = _debug_type_to_string (type);

  _init_debug ();

  switch (type) {
    case GL_DEBUG_TYPE_ERROR:
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOUR:
      GST_ERROR_OBJECT (context, "%s: GL %s from %s id:%u, %s", severity_str,
          type_str, source_str, id, message);
      break;
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOUR:
    case GL_DEBUG_TYPE_PORTABILITY:
      GST_FIXME_OBJECT (context, "%s: GL %s from %s id:%u, %s", severity_str,
          type_str, source_str, id, message);
      break;
    case GL_DEBUG_TYPE_PERFORMANCE:
      GST_CAT_DEBUG_OBJECT (gst_performance, context, "%s: GL %s from %s id:%u,"
          " %s", severity_str, type_str, source_str, id, message);
      break;
    default:
      GST_DEBUG_OBJECT (context, "%s: GL %s from %s id:%u, %s", severity_str,
          type_str, source_str, id, message);
      break;
  }
}

void
gst_gl_insert_debug_marker (GstGLContext * context, const gchar * format, ...)
{
  const GstGLFuncs *gl = context->gl_vtable;
  gchar *string;
  gint len;
  va_list args;

  va_start (args, format);
  len = gst_info_vasprintf (&string, format, args);
  va_end (args);

  /* gst_info_vasprintf() returns -1 on error, the various debug marker
   * functions take len=-1 to mean null terminated */
  if (len < 0 || string == NULL)
    /* no debug output */
    return;

  if (gl->DebugMessageInsert)
    gl->DebugMessageInsert (GL_DEBUG_SOURCE_THIRD_PARTY, GL_DEBUG_TYPE_MARKER,
        0, GL_DEBUG_SEVERITY_LOW, (gsize) len, string);
  else if (gl->InsertEventMarker)
    gl->InsertEventMarker (len, string);
  else if (gl->StringMarker)
    gl->StringMarker (len, string);

  g_free (string);
}

/**
 * gst_gl_async_debug_store_log_msg_valist:
 * @ad: the #GstGLAsyncDebug to store the message in
 * @cat: the #GstDebugCategory to output the message in
 * @level: the #GstLevel
 * @file: the file where the debug message originates from
 * @function: the function where the debug message originates from
 * @line: the line in @file where the debug message originates from
 * @object: (allow-none): a #GObject to associate with the debug message
 * @format: a printf style format string
 * @varargs: the list of arguments for @format
 *
 * Stores a debug message for later output by gst_gl_async_debug_output_log_msg()
 */
void
gst_gl_async_debug_store_log_msg_valist (GstGLAsyncDebug * ad,
    GstDebugCategory * cat, GstDebugLevel level, const gchar * file,
    const gchar * function, gint line, GObject * object, const gchar * format,
    va_list varargs)
{
  gst_gl_async_debug_output_log_msg (ad);
  _free_async_debug_data (ad);

  if (G_UNLIKELY (level <= GST_LEVEL_MAX && level <= _gst_debug_min)) {
    if (!cat)
      cat = default_debug;

    ad->cat = cat;
    ad->level = level;
    ad->file = file;
    ad->function = function;
    ad->line = line;
    if (object)
      ad->object = g_object_ref (object);
    else
      ad->object = NULL;

    ad->debug_msg = gst_info_strdup_vprintf (format, varargs);
    ad->state_flags |= ASYNC_DEBUG_FILLED;
  }
}

/**
 * gst_gl_async_debug_output_log_msg:
 * @ad: the #GstGLAsyncDebug to store the message in
 *
 * Outputs a previously stored debug message.
 */
void
gst_gl_async_debug_output_log_msg (GstGLAsyncDebug * ad)
{
  if ((ad->state_flags & ASYNC_DEBUG_FILLED) != 0
      && (ad->state_flags & ASYNC_DEBUG_FROZEN) == 0) {
    gchar *msg = NULL;

    if (ad->callback)
      msg = ad->callback (ad->user_data);

    gst_debug_log (ad->cat, ad->level, ad->file, ad->function, ad->line,
        ad->object, "%s %s", GST_STR_NULL (ad->debug_msg), msg ? msg : "");
    g_free (msg);
    _free_async_debug_data (ad);
  }
}

/**
 * gst_gl_async_debug_store_log_msg:
 * @ad: the #GstGLAsyncDebug to store the message in
 * @cat: the #GstDebugCategory to output the message in
 * @level: the #GstLevel
 * @file: the file where the debug message originates from
 * @function: the function where the debug message originates from
 * @line: the line in @file where the debug message originates from
 * @object: (allow-none): a #GObject to associate with the debug message
 * @format: a printf style format string
 * @...: the list of arguments for @format
 *
 * Stores a debug message for later output by gst_gl_async_debug_output_log_msg()
 */
void
gst_gl_async_debug_store_log_msg (GstGLAsyncDebug * ad, GstDebugCategory * cat,
    GstDebugLevel level, const gchar * file, const gchar * function, gint line,
    GObject * object, const gchar * format, ...)
{
  va_list varargs;

  if (G_UNLIKELY (level <= GST_LEVEL_MAX && level <= _gst_debug_min)) {
    va_start (varargs, format);
    gst_gl_async_debug_store_log_msg_valist (ad, cat, level, file, function,
        line, object, format, varargs);
    va_end (varargs);
  }
}
#endif
