/*
 * Copyright (C) 2011-2012 Andreas Steffen
 * HSR Hochschule fuer Technik Rapperswil
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include "imc_os_state.h"

#include <imc/imc_agent.h>
#include <imc/imc_msg.h>
#include <ietf/ietf_attr.h>
#include <ietf/ietf_attr_attr_request.h>
#include <ietf/ietf_attr_default_pwd_enabled.h>
#include <ietf/ietf_attr_fwd_enabled.h>
#include <ietf/ietf_attr_installed_packages.h>
#include <ietf/ietf_attr_numeric_version.h>
#include <ietf/ietf_attr_op_status.h>
#include <ietf/ietf_attr_product_info.h>
#include <ietf/ietf_attr_string_version.h>
#include <ita/ita_attr.h>
#include <ita/ita_attr_get_settings.h>
#include <ita/ita_attr_settings.h>
#include <ita/ita_attr_angel.h>
#include <os_info/os_info.h>

#include <tncif_pa_subtypes.h>

#include <pen/pen.h>
#include <utils/debug.h>

/* IMC definitions */

static const char imc_name[] = "OS";

static pen_type_t msg_types[] = {
	{ PEN_IETF, PA_SUBTYPE_IETF_OPERATING_SYSTEM }
};

static imc_agent_t *imc_os;
static os_info_t *os;

/**
 * see section 3.8.1 of TCG TNC IF-IMC Specification 1.3
 */
TNC_Result TNC_IMC_Initialize(TNC_IMCID imc_id,
							  TNC_Version min_version,
							  TNC_Version max_version,
							  TNC_Version *actual_version)
{
	if (imc_os)
	{
		DBG1(DBG_IMC, "IMC \"%s\" has already been initialized", imc_name);
		return TNC_RESULT_ALREADY_INITIALIZED;
	}
	imc_os = imc_agent_create(imc_name, msg_types, countof(msg_types),
							  imc_id, actual_version);
	if (!imc_os)
	{
		return TNC_RESULT_FATAL;
	}

	os = os_info_create();
	if (!os)
	{
		imc_os->destroy(imc_os);
		imc_os = NULL;

		return TNC_RESULT_FATAL;
	}

	if (min_version > TNC_IFIMC_VERSION_1 || max_version < TNC_IFIMC_VERSION_1)
	{
		DBG1(DBG_IMC, "no common IF-IMC version");
		return TNC_RESULT_NO_COMMON_VERSION;
	}
	return TNC_RESULT_SUCCESS;
}

/**
 * see section 3.8.2 of TCG TNC IF-IMC Specification 1.3
 */
TNC_Result TNC_IMC_NotifyConnectionChange(TNC_IMCID imc_id,
										  TNC_ConnectionID connection_id,
										  TNC_ConnectionState new_state)
{
	imc_state_t *state;

	if (!imc_os)
	{
		DBG1(DBG_IMC, "IMC \"%s\" has not been initialized", imc_name);
		return TNC_RESULT_NOT_INITIALIZED;
	}
	switch (new_state)
	{
		case TNC_CONNECTION_STATE_CREATE:
			state = imc_os_state_create(connection_id);
			return imc_os->create_state(imc_os, state);
		case TNC_CONNECTION_STATE_HANDSHAKE:
			if (imc_os->change_state(imc_os, connection_id, new_state,
				&state) != TNC_RESULT_SUCCESS)
			{
				return TNC_RESULT_FATAL;
			}
			state->set_result(state, imc_id,
							  TNC_IMV_EVALUATION_RESULT_DONT_KNOW);
			return TNC_RESULT_SUCCESS;
		case TNC_CONNECTION_STATE_DELETE:
			return imc_os->delete_state(imc_os, connection_id);
		default:
			return imc_os->change_state(imc_os, connection_id,
											 new_state, NULL);
	}
}

/**
 * Add IETF Product Information attribute to the send queue
 */
static void add_product_info(imc_msg_t *msg)
{
	pa_tnc_attr_t *attr;
	os_type_t os_type;
	pen_t vendor_id = PEN_IETF;
	int i;

	typedef struct vendor_pen_t {
		os_type_t os_type;
		pen_t pen;
	} vendor_pen_t;

	vendor_pen_t vendor_pens[] = {
		{ OS_TYPE_DEBIAN,  PEN_DEBIAN },
		{ OS_TYPE_UBUNTU,  PEN_CANONICAL },
		{ OS_TYPE_FEDORA,  PEN_FEDORA },
		{ OS_TYPE_REDHAT,  PEN_REDHAT },
		{ OS_TYPE_ANDROID, PEN_GOOGLE }
	};

	os_type = os->get_type(os);
	for (i = 0; i < countof(vendor_pens); i++)
	{
		if (os_type == vendor_pens[i].os_type)
		{
			vendor_id = vendor_pens[i].pen;
			break;
		}
	}
	attr = ietf_attr_product_info_create(vendor_id, 0, os->get_name(os));
	msg->add_attribute(msg, attr);
}

/**
 * Add IETF Numeric Version attribute to the send queue
 */
static void add_numeric_version(imc_msg_t *msg)
{
	pa_tnc_attr_t *attr;
	u_int32_t major, minor;

	os->get_numeric_version(os, &major, &minor);
	DBG1(DBG_IMC, "operating system numeric version is %d.%d",
				   major, minor);

	attr = ietf_attr_numeric_version_create(major, minor, 0, 0, 0);
	msg->add_attribute(msg, attr);
}

/**
 * Add IETF String Version attribute to the send queue
 */
static void add_string_version(imc_msg_t *msg)
{
	pa_tnc_attr_t *attr;

	attr = ietf_attr_string_version_create(os->get_version(os),
										   chunk_empty, chunk_empty);
	msg->add_attribute(msg, attr);
}

/**
 * Add IETF Operational Status attribute to the send queue
 */
static void add_op_status(imc_msg_t *msg)
{
	pa_tnc_attr_t *attr;
	time_t uptime, last_boot;

	uptime = os->get_uptime(os);
	last_boot = uptime ? time(NULL) - uptime : UNDEFINED_TIME;
	if (last_boot != UNDEFINED_TIME)
	{
		DBG1(DBG_IMC, "last boot: %T, %u s ago", &last_boot, TRUE, uptime);
	}
	attr = ietf_attr_op_status_create(OP_STATUS_OPERATIONAL,
									  OP_RESULT_SUCCESSFUL, last_boot);
	msg->add_attribute(msg, attr);
}

/**
 * Add IETF Forwarding Enabled attribute to the send queue
 */
static void add_fwd_enabled(imc_msg_t *msg)
{
	pa_tnc_attr_t *attr;
	os_fwd_status_t fwd_status;

	fwd_status = os->get_fwd_status(os);
	DBG1(DBG_IMC, "IPv4 forwarding status: %N",
				   os_fwd_status_names, fwd_status);
	attr = ietf_attr_fwd_enabled_create(fwd_status);
	msg->add_attribute(msg, attr);
}

/**
 * Add IETF Factory Default Password Enabled attribute to the send queue
 */
static void add_default_pwd_enabled(imc_msg_t *msg)
{
	pa_tnc_attr_t *attr;

	DBG1(DBG_IMC, "factory default password: disabled");
	attr = ietf_attr_default_pwd_enabled_create(FALSE);
	msg->add_attribute(msg, attr);
}

/**
 * Add an IETF Installed Packages attribute to the send queue
 */
static void add_installed_packages(imc_state_t *state, imc_msg_t *msg)
{
	pa_tnc_attr_t *attr = NULL, *attr_angel;
	ietf_attr_installed_packages_t *attr_cast;
	enumerator_t *enumerator;
	chunk_t name, version;
	size_t max_attr_size, attr_size, entry_size;
	bool first = TRUE;

	/**
	 * Compute the maximum IETF Installed Packages attribute size
	 * leaving space for an additional ITA Angel attribute
	 */
	max_attr_size = state->get_max_msg_len(state) - 8 - 12;

	/* At least one IETF Installed Packages attribute is sent */
	attr = ietf_attr_installed_packages_create();
	attr_size = 12 + 4;

	enumerator = os->create_package_enumerator(os);
	if (enumerator)
	{
		while (enumerator->enumerate(enumerator, &name, &version))
		{
			DBG2(DBG_IMC, "package '%.*s' (%.*s)",
						   name.len, name.ptr, version.len, version.ptr);

			entry_size = 2 + name.len + version.len;
			if (attr_size + entry_size > max_attr_size)
			{
				if (first)
				{
					/**
					 * Send an ITA Start Angel attribute to the IMV signalling
					 * that multiple ITA Installed Package attributes follow.
					 */
					attr_angel = ita_attr_angel_create(TRUE);
					msg->add_attribute(msg, attr_angel);
					first = FALSE;
				}
				msg->add_attribute(msg, attr);

				/* create the next IETF Installed Packages attribute */
				attr = ietf_attr_installed_packages_create();
				attr_size = 12 + 4;
			}
			attr_cast = (ietf_attr_installed_packages_t*)attr;
			attr_cast->add(attr_cast, name, version);
			attr_size += entry_size;
		}
		enumerator->destroy(enumerator);
	}
	msg->add_attribute(msg, attr);

	if (!first)
	{
		/**
		 * If we sent an ITA Start Angel attribute in the first place,
		 * terminate by appending a matching ITA Stop Angel attribute.
		 */
		attr_angel = ita_attr_angel_create(FALSE);
		msg->add_attribute(msg, attr_angel);
	}
}

/**
 * Add ITA Settings attribute to the send queue
 */
static void add_settings(enumerator_t *enumerator, imc_msg_t *msg)
{
	pa_tnc_attr_t *attr = NULL;
	ita_attr_settings_t *attr_cast;
	chunk_t value;
	char *name;
	bool first = TRUE;

	while (enumerator->enumerate(enumerator, &name))
	{
		DBG1(DBG_IMC, "setting '%s'", name);

		value = os->get_setting(os, name);
		if (!value.ptr)
		{
			continue;
		}
		if (first)
		{
			attr = ita_attr_settings_create();
			first = FALSE;
		}
		attr_cast = (ita_attr_settings_t*)attr;
		attr_cast->add(attr_cast, name, value);
		chunk_free(&value);
	}

	if (attr)
	{
		msg->add_attribute(msg, attr);
	}
}

/**
 * see section 3.8.3 of TCG TNC IF-IMC Specification 1.3
 */
TNC_Result TNC_IMC_BeginHandshake(TNC_IMCID imc_id,
								  TNC_ConnectionID connection_id)
{
	imc_state_t *state;
	imc_msg_t *out_msg;
	TNC_Result result = TNC_RESULT_SUCCESS;

	if (!imc_os)
	{
		DBG1(DBG_IMC, "IMC \"%s\" has not been initialized", imc_name);
		return TNC_RESULT_NOT_INITIALIZED;
	}
	if (!imc_os->get_state(imc_os, connection_id, &state))
	{
		return TNC_RESULT_FATAL;
	}
	if (lib->settings->get_bool(lib->settings,
								"libimcv.plugins.imc-os.push_info", TRUE))
	{
		out_msg = imc_msg_create(imc_os, state, connection_id, imc_id,
								 TNC_IMVID_ANY, msg_types[0]);
		add_product_info(out_msg);
		add_string_version(out_msg);
		add_numeric_version(out_msg);
		add_op_status(out_msg);
		add_fwd_enabled(out_msg);
		add_default_pwd_enabled(out_msg);

		/* send PA-TNC message with the excl flag not set */
		result = out_msg->send(out_msg, FALSE);
		out_msg->destroy(out_msg);
	}

	return result;
}

static TNC_Result receive_message(imc_state_t *state, imc_msg_t *in_msg)
{
	imc_msg_t *out_msg;
	enumerator_t *enumerator;
	pa_tnc_attr_t *attr;
	pen_type_t type;
	TNC_Result result;
	bool fatal_error = FALSE;

	/* parse received PA-TNC message and handle local and remote errors */
	result = in_msg->receive(in_msg, &fatal_error);
	if (result != TNC_RESULT_SUCCESS)
	{
		return result;
	}
	out_msg = imc_msg_create_as_reply(in_msg);

	/* analyze PA-TNC attributes */
	enumerator = in_msg->create_attribute_enumerator(in_msg);
	while (enumerator->enumerate(enumerator, &attr))
	{
		type = attr->get_type(attr);

		if (type.vendor_id == PEN_IETF)
		{
			if (type.type == IETF_ATTR_ATTRIBUTE_REQUEST)
			{
				ietf_attr_attr_request_t *attr_cast;
				pen_type_t *entry;
				enumerator_t *e;

				attr_cast = (ietf_attr_attr_request_t*)attr;

				e = attr_cast->create_enumerator(attr_cast);
				while (e->enumerate(e, &entry))
				{
					if (entry->vendor_id != PEN_IETF)
					{
						continue;
					}
					switch (entry->type)
					{
						case IETF_ATTR_PRODUCT_INFORMATION:
							add_product_info(out_msg);
							break;
						case IETF_ATTR_STRING_VERSION:
							add_string_version(out_msg);
							break;
						case IETF_ATTR_NUMERIC_VERSION:
							add_numeric_version(out_msg);
							break;
						case IETF_ATTR_OPERATIONAL_STATUS:
							add_op_status(out_msg);
							break;
						case IETF_ATTR_FORWARDING_ENABLED:
							add_fwd_enabled(out_msg);
							break;
						case IETF_ATTR_FACTORY_DEFAULT_PWD_ENABLED:
							add_default_pwd_enabled(out_msg);
							break;
						case IETF_ATTR_INSTALLED_PACKAGES:
							add_installed_packages(state, out_msg);
							break;
						default:
							break;
					}
				}
				e->destroy(e);
			}
		}
		else if (type.vendor_id == PEN_ITA && type.type == ITA_ATTR_GET_SETTINGS)
		{
			ita_attr_get_settings_t *attr_cast;
			enumerator_t *e;

			attr_cast = (ita_attr_get_settings_t*)attr;

			e = attr_cast->create_enumerator(attr_cast);
			add_settings(e, out_msg);
			e->destroy(e);
		}
	}
	enumerator->destroy(enumerator);

	if (fatal_error)
	{
		result = TNC_RESULT_FATAL;
	}
	else
	{
		result = out_msg->send(out_msg, TRUE);
	}
	out_msg->destroy(out_msg);

	return result;
}

/**
 * see section 3.8.4 of TCG TNC IF-IMC Specification 1.3

 */
TNC_Result TNC_IMC_ReceiveMessage(TNC_IMCID imc_id,
								  TNC_ConnectionID connection_id,
								  TNC_BufferReference msg,
								  TNC_UInt32 msg_len,
								  TNC_MessageType msg_type)
{
	imc_state_t *state;
	imc_msg_t *in_msg;
	TNC_Result result;

	if (!imc_os)
	{
		DBG1(DBG_IMC, "IMC \"%s\" has not been initialized", imc_name);
		return TNC_RESULT_NOT_INITIALIZED;
	}
	if (!imc_os->get_state(imc_os, connection_id, &state))
	{
		return TNC_RESULT_FATAL;
	}
	in_msg = imc_msg_create_from_data(imc_os, state, connection_id, msg_type,
									  chunk_create(msg, msg_len));
	result = receive_message(state, in_msg);
	in_msg->destroy(in_msg);

	return result;
}

/**
 * see section 3.8.6 of TCG TNC IF-IMV Specification 1.3
 */
TNC_Result TNC_IMC_ReceiveMessageLong(TNC_IMCID imc_id,
									  TNC_ConnectionID connection_id,
									  TNC_UInt32 msg_flags,
									  TNC_BufferReference msg,
									  TNC_UInt32 msg_len,
									  TNC_VendorID msg_vid,
									  TNC_MessageSubtype msg_subtype,
									  TNC_UInt32 src_imv_id,
									  TNC_UInt32 dst_imc_id)
{
	imc_state_t *state;
	imc_msg_t *in_msg;
	TNC_Result result;

	if (!imc_os)
	{
		DBG1(DBG_IMC, "IMC \"%s\" has not been initialized", imc_name);
		return TNC_RESULT_NOT_INITIALIZED;
	}
	if (!imc_os->get_state(imc_os, connection_id, &state))
	{
		return TNC_RESULT_FATAL;
	}
	in_msg = imc_msg_create_from_long_data(imc_os, state, connection_id,
								src_imv_id, dst_imc_id,msg_vid, msg_subtype,
								chunk_create(msg, msg_len));
	result =receive_message(state, in_msg);
	in_msg->destroy(in_msg);

	return result;
}

/**
 * see section 3.8.7 of TCG TNC IF-IMC Specification 1.3
 */
TNC_Result TNC_IMC_BatchEnding(TNC_IMCID imc_id,
							   TNC_ConnectionID connection_id)
{
	if (!imc_os)
	{
		DBG1(DBG_IMC, "IMC \"%s\" has not been initialized", imc_name);
		return TNC_RESULT_NOT_INITIALIZED;
	}
	return TNC_RESULT_SUCCESS;
}

/**
 * see section 3.8.8 of TCG TNC IF-IMC Specification 1.3
 */
TNC_Result TNC_IMC_Terminate(TNC_IMCID imc_id)
{
	if (!imc_os)
	{
		DBG1(DBG_IMC, "IMC \"%s\" has not been initialized", imc_name);
		return TNC_RESULT_NOT_INITIALIZED;
	}
	imc_os->destroy(imc_os);
	imc_os = NULL;

	os->destroy(os);
	os = NULL;

	return TNC_RESULT_SUCCESS;
}

/**
 * see section 4.2.8.1 of TCG TNC IF-IMC Specification 1.3
 */
TNC_Result TNC_IMC_ProvideBindFunction(TNC_IMCID imc_id,
									   TNC_TNCC_BindFunctionPointer bind_function)
{
	if (!imc_os)
	{
		DBG1(DBG_IMC, "IMC \"%s\" has not been initialized", imc_name);
		return TNC_RESULT_NOT_INITIALIZED;
	}
	return imc_os->bind_functions(imc_os, bind_function);
}
