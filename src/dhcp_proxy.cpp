/*
 * libasiotap - A portable TAP adapter extension for Boost::ASIO.
 * Copyright (C) 2010-2011 Julien KAUFFMANN <julien.kauffmann@freelan.org>
 *
 * This file is part of libasiotap.
 *
 * libasiotap is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * libasiotap is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 *
 * If you intend to use libasiotap in a commercial software, please
 * contact me : we may arrange this for a small fee or no fee at all,
 * depending on the nature of your project.
 */

/**
 * \file dhcp_proxy.cpp
 * \author Julien KAUFFMANN <julien.kauffmann@freelan.org>
 * \brief An DHCP proxy class.
 */

#include "osi/dhcp_proxy.hpp"

#include "osi/ethernet_helper.hpp"
#include "osi/ipv4_helper.hpp"
#include "osi/udp_helper.hpp"
#include "osi/bootp_helper.hpp"
#include "osi/dhcp_helper.hpp"

#include "osi/ethernet_builder.hpp"
#include "osi/ipv4_builder.hpp"
#include "osi/udp_builder.hpp"
#include "osi/bootp_builder.hpp"
#include "osi/dhcp_builder.hpp"

#include <boost/foreach.hpp>

namespace asiotap
{
	namespace osi
	{
		namespace
		{
			bool is_bootp_request(const_helper<bootp_frame> bootp_helper)
			{
				return (bootp_helper.operation() == BOOTP_BOOTREQUEST);
			}

			bool bootp_is_ethernet(const_helper<bootp_frame> bootp_helper)
			{
				return ((bootp_helper.hardware_type() == BOOTP_HARDWARE_TYPE_ETHERNET) && (bootp_helper.hardware_length() == ETHERNET_ADDRESS_SIZE));
			}

			proxy<dhcp_frame>::ethernet_address_type bootp_get_ethernet_address(const_helper<bootp_frame> bootp_helper)
			{
				proxy<dhcp_frame>::ethernet_address_type result;

				std::memcpy(result.data(), boost::asio::buffer_cast<const void*>(bootp_helper.chaddr()), result.size());

				return result;
			}
		}

		void proxy<dhcp_frame>::do_handle_frame(
				const_helper<ethernet_frame> ethernet_helper,
				const_helper<ipv4_frame> ipv4_helper,
				const_helper<udp_frame> udp_helper,
				const_helper<bootp_frame> bootp_helper,
				const_helper<dhcp_frame> dhcp_helper
				)
		{
			// This implementation is partial and far from being perfect.
			// In a ideal world, there should be some udp_socket and a real (complete) DHCP server implementation.

			if (is_bootp_request(bootp_helper) && bootp_is_ethernet(bootp_helper))
			{
				const ethernet_address_type ethernet_address_source = bootp_get_ethernet_address(bootp_helper);

				const entry_map_type::const_iterator entry = m_entry_map.find(ethernet_address_source);

				// A matching entry was found.
				if (entry != m_entry_map.end())
				{
					size_t payload_size;

					builder<dhcp_frame> dhcp_builder(response_buffer());

					BOOST_FOREACH(dhcp_option_helper<const_helper_tag>& dhcp_option_helper, dhcp_helper)
					{
						switch (dhcp_option_helper.tag())
						{
							case dhcp_option::dhcp_message_type:
								{
									switch (dhcp_option_helper.value_as<uint8_t>())
									{
										case DHCP_DISCOVER_MESSAGE:
											{
												dhcp_builder.add_option(dhcp_option::dhcp_message_type, DHCP_OFFER_MESSAGE);
												break;
											}
										case DHCP_REQUEST_MESSAGE:
											{
												dhcp_builder.add_option(dhcp_option::dhcp_message_type, DHCP_ACKNOWLEDGMENT_MESSAGE);
												break;
											}
									}
									break;
								}
							default:
								{
									break;
								}
						}
					}

					dhcp_builder.add_option(dhcp_option::end);
					dhcp_builder.complete_padding(60);
					payload_size = dhcp_builder.write();

					builder<bootp_frame> bootp_builder(response_buffer(), payload_size);

					payload_size = bootp_builder.write(
							BOOTP_BOOTREPLY,
							bootp_helper.hardware_type(),
							bootp_helper.hardware_length(),
							bootp_helper.hops(),
							bootp_helper.xid(),
							bootp_helper.seconds(),
							bootp_helper.flags(),
							boost::asio::ip::address_v4::any(),
							boost::asio::ip::address_v4::any(),
							m_software_address,
							boost::asio::ip::address_v4::any(),
							boost::asio::buffer(ethernet_address_source),
							boost::asio::const_buffer(NULL, 0),
							boost::asio::const_buffer(NULL, 0)
							);

					builder<udp_frame> udp_builder(response_buffer(), payload_size);

					payload_size = udp_builder.write(udp_helper.destination(), udp_helper.source());

					builder<ipv4_frame> ipv4_builder(response_buffer(), payload_size);

					payload_size = ipv4_builder.write(
							ipv4_helper.tos(),
							ipv4_helper.identification(),
							ipv4_helper.flags(),
							ipv4_helper.position_fragment(),
							ipv4_helper.ttl(),
							ipv4_helper.protocol(),
							m_software_address,
							ipv4_helper.source()
							);

					udp_builder.update_checksum(ipv4_builder.get_helper());

					builder<ethernet_frame> ethernet_builder(response_buffer(), payload_size);

					payload_size = ethernet_builder.write(ethernet_helper.sender(), boost::asio::buffer(m_hardware_address), ethernet_helper.protocol());

					data_available(get_truncated_response_buffer(payload_size));
				}
			}
		}
	}
}
