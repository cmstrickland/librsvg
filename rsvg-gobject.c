/* vim: set sw=4: -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
   rsvg-gobject.c: GObject support.

   Copyright (C) 2006 Robert Staudinger <robert.staudinger@gmail.com>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/

#include "rsvg-private.h"
#include "rsvg-defs.h"

enum {
	PROP_0,	
	PROP_DPI_X,
	PROP_DPI_Y,
	PROP_BASE_URI,
	PROP_TITLE, 
	PROP_DESC,
	PROP_METADATA,
	NUM_PROPS
};

extern double rsvg_internal_dpi_x;
extern double rsvg_internal_dpi_y;

static GObjectClass *rsvg_parent_class = NULL;

static void
instance_init (RsvgHandle *self)
{
	self->defs = rsvg_defs_new ();
	self->handler_nest = 0;
	self->entities = g_hash_table_new (g_str_hash, g_str_equal);
	self->dpi_x = rsvg_internal_dpi_x;
	self->dpi_y = rsvg_internal_dpi_y;
	
	self->css_props = g_hash_table_new_full (g_str_hash, g_str_equal,
											   g_free, g_free);
	rsvg_SAX_handler_struct_init ();
	
	self->ctxt = NULL;
	self->currentnode = NULL;
	self->treebase = NULL;

	self->finished = 0;
	self->first_write = TRUE;

	self->is_disposed = FALSE;
}

static void
rsvg_ctx_free_helper (gpointer key, gpointer value, gpointer user_data)
{
	xmlEntityPtr entval = (xmlEntityPtr)value;
	
	/* key == entval->name, so it's implicitly freed below */
	
	g_free ((char *) entval->name);
	g_free ((char *) entval->ExternalID);
	g_free ((char *) entval->SystemID);
	xmlFree (entval->content);
	xmlFree (entval->orig);
	g_free (entval);
}

static void
instance_dispose (GObject *instance)
{
	RsvgHandle *self = (RsvgHandle*) instance;

	if (self->is_disposed)
		return;

	self->is_disposed = TRUE;

#if HAVE_SVGZ
	if (self->is_gzipped)
		g_object_unref (G_OBJECT (self->gzipped_data));
#endif

	g_hash_table_foreach (self->entities, rsvg_ctx_free_helper, NULL);
	g_hash_table_destroy (self->entities);
	rsvg_defs_free (self->defs);
	g_hash_table_destroy (self->css_props);
	
	if (self->user_data_destroy)
		(* self->user_data_destroy) (self->user_data);

	if (self->title)
		g_string_free (self->title, TRUE);
	if (self->desc)
		g_string_free (self->desc, TRUE);
	if (self->metadata)
		g_string_free (self->metadata, TRUE);
	if (self->base_uri)
		g_free (self->base_uri);

	rsvg_parent_class->dispose (G_OBJECT (self));
}

static void
set_property (GObject      *instance,
			  guint         prop_id,
			  GValue const *value,
			  GParamSpec   *pspec)
{
	RsvgHandle *self = RSVG_HANDLE (instance);

	switch (prop_id) {
	case PROP_DPI_X:
		rsvg_handle_set_dpi_x_y (self, g_value_get_double (value), self->dpi_y);
		break;
	case PROP_DPI_Y:
		rsvg_handle_set_dpi_x_y (self, self->dpi_x, g_value_get_double (value));
		break;
	case PROP_BASE_URI:
		rsvg_handle_set_base_uri (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (instance, prop_id, pspec);
	}
}

static void
get_property (GObject    *instance,
			  guint       prop_id,
			  GValue     *value,
			  GParamSpec *pspec)
{
	RsvgHandle *self = RSVG_HANDLE (instance);

	switch (prop_id) {
	case PROP_DPI_X:
		g_value_set_double (value, self->dpi_x);
		break;
	case PROP_DPI_Y:
		g_value_set_double (value, self->dpi_y);
		break;
	case PROP_BASE_URI:
		g_value_set_string (value, rsvg_handle_get_base_uri (self));
		break;
	case PROP_TITLE:
		g_value_set_string (value, rsvg_handle_get_title (self));
		break;
	case PROP_DESC:
		g_value_set_string (value, rsvg_handle_get_desc (self));
		break;
	case PROP_METADATA:
		g_value_set_string (value, rsvg_handle_get_metadata (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (instance, prop_id, pspec);
	}
}

static void
class_init (RsvgHandleClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	/* hook gobject vfuncs */
	gobject_class->dispose = instance_dispose;

	rsvg_parent_class = (GObjectClass*) g_type_class_peek_parent (klass);

	gobject_class->set_property = set_property;
	gobject_class->get_property = get_property;

	/**
	 * dpi-x:
	 */
	g_object_class_install_property (gobject_class,
		PROP_DPI_X,
		g_param_spec_double ("dpi-x", _("Horizontal resolution"),
			_("Horizontal resolution"), 0., G_MAXDOUBLE, rsvg_internal_dpi_x,
			(GParamFlags)(G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));

	g_object_class_install_property (gobject_class,
		PROP_DPI_Y,
		g_param_spec_double ("dpi-y", _("Vertical resolution"),
			_("Vertical resolution"), 0., G_MAXDOUBLE, rsvg_internal_dpi_y,
			(GParamFlags)(G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));

	g_object_class_install_property (gobject_class,
		PROP_BASE_URI,
		g_param_spec_string ("base-uri", _("Base URI"),
			_("Base URI"), NULL, 
			(GParamFlags)(G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));

	g_object_class_install_property (gobject_class,
		PROP_TITLE,
		g_param_spec_string ("title", _("Title"),
			_("SVG file title"), NULL, 
			(GParamFlags)(G_PARAM_READABLE)));

	g_object_class_install_property (gobject_class,
		PROP_DESC,
		g_param_spec_string ("desc", _("Description"),
			_("SVG file description"), NULL, 
			(GParamFlags)(G_PARAM_READABLE)));

	g_object_class_install_property (gobject_class,
		PROP_METADATA,
		g_param_spec_string ("metadata", _("Metadata"),
			_("SVG file metadata"), NULL, 
			(GParamFlags)(G_PARAM_READABLE)));
}

GType
rsvg_handle_get_type (void)
{
	static GType type = 0;
	if (!type) {
		static const GTypeInfo info = {
			sizeof (RsvgHandleClass),
			NULL,           /* base_init */
			NULL,           /* base_finalize */
			(GClassInitFunc) class_init,
			NULL,           /* class_finalize */
			NULL,           /* class_data */
			sizeof (RsvgHandle),
			0,              /* n_preallocs */
			(GInstanceInitFunc) instance_init,
		};

		type = g_type_register_static (G_TYPE_OBJECT, "RsvgHandle", &info, (GTypeFlags)0);
	}
	return type;
}

/**
 * rsvg_handle_new:
 *
 * Returns a new rsvg handle.  Must be freed with @rsvg_handle_free.  This
 * handle can be used for dynamically loading an image.  You need to feed it
 * data using @rsvg_handle_write, then call @rsvg_handle_close when done.  No
 * more than one image can be loaded with one handle.
 *
 * Returns: A new #RsvgHandle
 **/
RsvgHandle *
rsvg_handle_new (void)
{
	return RSVG_HANDLE (g_object_new (RSVG_TYPE_HANDLE, NULL));
}
