# Makefile.am for Http
#
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

# SUBDIRS = acl parser

AM_CPPFLAGS = \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/lib \
  -I$(top_srcdir)/lib/records \
  -I$(top_srcdir)/lib/ts \
  -I$(top_builddir)/proxy \
  -I$(top_builddir)/proxy/api/ts \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/mgmt \
  -I$(top_srcdir)/mgmt/preparse \
  -I$(top_srcdir)/mgmt/utils \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy/http \
  -I$(top_srcdir)/proxy/http/healthcheck \
  -I$(top_srcdir)/proxy/http/remap/acl   \
  -I$(top_srcdir)/proxy/http/remap/parser

noinst_LIBRARIES = libhttp_remap.a

libhttp_remap_a_SOURCES = \
  AclFiltering.cc \
  AclFiltering.h  \
  RemapPluginInfo.cc \
  RemapPluginInfo.h  \
  RemapPlugins.cc \
  RemapPlugins.h  \
  RemapProcessor.cc \
  RemapProcessor.h  \
  UrlMapping.cc \
  UrlMapping.h  \
  UrlRewrite.cc \
  UrlRewrite.h  \
  UrlMappingPathIndex.h  \
  UrlMappingPathIndex.cc \
	MappingTypes.h  \
  HostnameTrie.h  \
  HostnameTrie.cc \
	UrlMappingRegexMatcher.cc \
	UrlMappingPathContainer.cc \
  acl/ACLChecker.h    \
  acl/ACLChecker.cc   \
  acl/ACLCheckList.h  \
  acl/ACLCheckList.cc \
  acl/ACLDefineChecker.h  \
  acl/ACLDefineChecker.cc \
  acl/ACLDefineManager.h  \
  acl/ACLDefineManager.cc \
	acl/ACLMethodIpCheckList.h  \
  acl/ACLMethodIpCheckList.cc \
  acl/ACLRefererCheckList.h   \
  acl/ACLRefererCheckList.cc  \
	parser/ACLDirective.h   \
	parser/ACLDirective.cc  \
	parser/ACLParams.h   \
	parser/ACLParams.cc  \
	parser/ConfigDirective.h   \
	parser/ConfigDirective.cc  \
	parser/ConfigParams.h      \
	parser/ConfigParams.cc     \
	parser/DirectiveParams.h   \
	parser/DirectiveParams.cc  \
	parser/MappingDirective.h  \
	parser/MappingDirective.cc \
	parser/MappingEntry.h    \
	parser/MappingEntry.cc   \
	parser/MappingManager.h  \
	parser/MappingManager.cc \
	parser/MappingParams.h   \
	parser/MappingParams.cc  \
	parser/PluginDirective.h \
	parser/PluginDirective.cc\
	parser/PluginParams.h    \
	parser/PluginParams.cc   \
	parser/RemapDirective.h  \
	parser/RemapDirective.cc \
	parser/RemapParser.h     \
	parser/RemapParser.cc    \
	parser/SchemeDirective.h \
	parser/SchemeDirective.cc\
	parser/SchemeParams.h    \
	parser/SchemeParams.cc   \
	parser/IncludeDirective.h  \
	parser/IncludeDirective.cc \
	parser/IncludeParams.h   \
	parser/IncludeParams.cc

