#
#
# Makefile.am:
#
# Copyright (c) 2009 James McKenzie <20@madingley.org>,
# All rights reserved.
#
# $Id:$
#
# $Log:$
#
#
#

#
# Copyright (c) 2013 Citrix Systems, Inc.
# 
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
# 
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
# 
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
#


# NB you must manually add the object implementation to SRCS
# as automake can't cope with runtime source file lists

DBUS_CLIENT_IDLS=db surfman xenmgr xenmgr_host xenmgr_vm xcpmd
DBUS_SERVER_IDLS=input_daemon


SRCS=server.c bus.c secure_scripts.c user.c secure.c input.c domains.c \
     switch.c util.c focus.c touchpad.c keymap.c usb-tablet.c rpcgen/input_daemon_server_obj.c \
     pm.c xen_vkbd.c xen_event.c gesture.c lid.c encapsulate.c socket.c

LIBS=-lz -lssl -levent @UDEV_LIBS@ @DBUS_LIBS@ @DBUS_GLIB_LIBS@ @LIBXCDBUS_LIBS@ @LIBXC_LIB@ @LIBXCXENSTORE_LIBS@ @LIBDMBUS_LIB@ @LIBV4V_LIB@ @LIBXENBACKEND_LIB@

CPROTO=cproto
INCLUDES = @UDEV_CFLAGS@ @DBUS_CFLAGS@ @DBUS_GLIB_CFLAGS@ @LIBXCDBUS_CFLAGS@ @LIBXC_INC@ @LIBXCXENSTORE_CFLAGS@ @LIBDMBUS_INC@ @LIBXENBACKEND_INC@

fish:
	echo ${input_server_SOURCES}

include_HEADERS=xc_input_socket_protocol.h

bin_PROGRAMS = input_server


BUILT_SOURCES = ${DBUS_CLIENT_IDLS:%=rpcgen/%_client.h} \
	${DBUS_SERVER_IDLS:%=rpcgen/%_server_marshall.h} \
	${DBUS_SERVER_IDLS:%=rpcgen/%_server_obj.c} \
	${DBUS_SERVER_IDLS:%=rpcgen/%_server_obj.h} 


input_server_SOURCES = ${SRCS} main.c

#input_client_SOURCES = ${SRCS} client.c

#intel_putpic_SOURCES = ${SRCS} putpic.c
#intel_putpic_LDADD = ${LIBS}

AM_CFLAGS=-g -W -Wall -Werror -Wno-unused -D_GNU_SOURCE

rpcgen/%_client.h: ${IDLDIR}/%.xml
	mkdir rpcgen || true
	${XC_RPCGEN} -c -o rpcgen/ $<

rpcgen/%_server_marshall.h rpcgen/%_server_obj.h rpcgen/%_server_obj.c : ${IDLDIR}/%.xml
	mkdir rpcgen || true
	${XC_RPCGEN} -s -o rpcgen/ $<

protos:
	echo > prototypes.h
	${CPROTO} -E "${CPP} ${CPPFLAGS}" -v `echo ${INCLUDES} | sed -e 's/-pthread//g'` ${SRCS} > prototypes.tmp
	mv -f prototypes.tmp prototypes.h

