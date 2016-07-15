# Project: server

DEBUG := yes
# DEBUG := no

#****************************************************************************
OUT_DIR := out/
AHTTP_LIB_DIR := ahttplib/
ACONNECT_LIB_DIR := aconnectlib/

AHTTPSERVER_DIR := ahttpserver/
HANDLER_PYTHON_DIR :=  handler_python/
MOD_BASIC_AUTH_DIR :=  module_authbasic/

ACONNECT_DIR := aconnect/
AHTTP_DIR := ahttp/
TINYXML_DIR := tinyxml/


#****************************************************************************
CXX		:= g++
LIBS	:= -lboost_regex-gcc41 -lboost_thread-gcc41-mt -lboost_date_time-gcc41\
			-lboost_filesystem-gcc41 -lboost_python-gcc41 -lpthread -lutil -ldl -lpython2.5

#-Wl,--no-allow-shlib-undefined

DEBUG_CFLAGS     	:= -Wall -Wno-unknown-pragmas -Wno-format -g -DDEBUG -I$(AHTTP_LIB_DIR) -I$(ACONNECT_LIB_DIR) -I/usr/include/python2.5/
RELEASE_CFLAGS   	:= -Wall -Wno-unknown-pragmas -I$(AHTTP_LIB_DIR) -I$(ACONNECT_LIB_DIR)  -I/usr/include/python2.5/

DEBUG_CXXFLAGS   := ${DEBUG_CFLAGS}
RELEASE_CXXFLAGS := ${RELEASE_CFLAGS}

DEBUG_LDFLAGS    := $(LIBS)
RELEASE_LDFLAGS  := $(LIBS)

RELEASE_BUILD_DIR := Release/
DEBUG_BUILD_DIR := Debug/

SERVER_EXE_NAME := ahttpserver

RELEASE_ACONNECT_LIB_NAME := libaconnect.a
DEBUG_ACONNECT_LIB_NAME := libaconnect-d.a

RELEASE_AHTTP_LIB_NAME := libahttp.a
DEBUG_AHTTP_LIB_NAME := libahttp-d.a

RELEASE_HANDLER_PYTHON_NAME := handler_python.so
DEBUG_HANDLER_PYTHON_NAME := handler_python-d.so

RELEASE_MOD_BASIC_AUTH_NAME := module_authbasic.so
DEBUG_MOD_BASIC_AUTH_NAME := module_authbasic-d.so

ifeq (yes, ${DEBUG})
	SERVER_EXE_NAME := $(SERVER_EXE_NAME)-d
	CFLAGS       := ${DEBUG_CFLAGS}
	CXXFLAGS     := ${DEBUG_CXXFLAGS}
	LDFLAGS      := ${DEBUG_LDFLAGS}
	BUILD_DIR    := $(DEBUG_BUILD_DIR)
	ACONNECT_LIB_NAME := $(DEBUG_ACONNECT_LIB_NAME)
	AHTTP_LIB_NAME := $(DEBUG_AHTTP_LIB_NAME)
	HANDLER_PYTHON_NAME := $(DEBUG_HANDLER_PYTHON_NAME)
	MOD_BASIC_AUTH_NAME := $(DEBUG_MOD_BASIC_AUTH_NAME)
else
	CFLAGS       := ${RELEASE_CFLAGS}
	CXXFLAGS     := ${RELEASE_CXXFLAGS}
	LDFLAGS      := ${RELEASE_LDFLAGS}
	BUILD_DIR    := $(RELEASE_BUILD_DIR)
	ACONNECT_LIB_NAME := $(RELEASE_ACONNECT_LIB_NAME)
	AHTTP_LIB_NAME := $(RELEASE_AHTTP_LIB_NAME)
	HANDLER_PYTHON_NAME := $(RELEASE_HANDLER_PYTHON_NAME)
    MOD_BASIC_AUTH_NAME := $(RELEASE_MOD_BASIC_AUTH_NAME)
endif


#****************************************************************************
# sources
#****************************************************************************
ACONNECT_SRCS := error.cpp logger.cpp util.cpp util.network.cpp  aconnect.cpp password_file_storage.cpp
ACONNECT_OBJS := $(addsuffix .o, $(basename ${ACONNECT_SRCS}) )

AHTTP_SRCS := http_request.cpp  http_response.cpp  http_response_header.cpp  http_context.cpp http_server.cpp  http_server_settings.cpp  http_support.cpp
AHTTP_OBJS := $(addsuffix .o, $(basename ${AHTTP_SRCS}) )

TXML_SRCS := tinyxml.cpp tinyxmlparser.cpp tinyxmlerror.cpp tinystr.cpp
TXML_OBJS := $(addsuffix .o, $(basename ${TXML_SRCS}))

PY_HND_SRCS := handler_python.cpp wrappers.cpp
PY_HND_OBJS := $(addsuffix .o, $(basename ${PY_HND_SRCS}))

MOD_BASIC_AUTH_SRCS := auth_provider_server.cpp auth_provider_system.cpp module_authbasic.cpp
MOD_BASIC_AUTH_OBJS := $(addsuffix .o, $(basename ${MOD_BASIC_AUTH_SRCS}))

AHTTP_LIB_BUILD_DIR := $(AHTTP_LIB_DIR)$(BUILD_DIR)
ACONNECT_LIB_BUILD_DIR := $(ACONNECT_LIB_DIR)$(BUILD_DIR)

PY_HND_BUILD_DIR := $(HANDLER_PYTHON_DIR)$(BUILD_DIR)
MOD_BASIC_AUTH_BUILD_DIR := $(MOD_BASIC_AUTH_DIR)$(BUILD_DIR)

ACONNECT_SRC_DIR := $(ACONNECT_LIB_DIR)$(ACONNECT_DIR)
AHHTP_SRC_DIR := $(AHTTP_LIB_DIR)$(AHTTP_DIR)
TINYXML_SRC_DIR := $(AHTTP_LIB_DIR)$(TINYXML_DIR)

ACONNECT_LIB_OBJS := 	$(addprefix ${ACONNECT_LIB_BUILD_DIR}, ${ACONNECT_OBJS})
AHTTP_LIB_OBJS := 	$(addprefix ${AHTTP_LIB_BUILD_DIR}, ${AHTTP_OBJS})\
					$(addprefix ${AHTTP_LIB_BUILD_DIR}, ${TXML_OBJS})

DEPENDENCIES := $(subst .o,.d, ${ACONNECT_LIB_OBJS}) \
 				$(subst .o,.d, ${AHTTP_LIB_OBJS})

PY_HND_OBJS_FULL := $(addprefix ${PY_HND_BUILD_DIR}, ${PY_HND_OBJS})
MOD_BASIC_AUTH_OBJS_FULL := $(addprefix ${MOD_BASIC_AUTH_BUILD_DIR}, ${MOD_BASIC_AUTH_OBJS})

#****************************************************************************
# Targets of the build
#****************************************************************************
.PHONY: all aconnectlib ahttplib handler_python module_authbasic ahttpserver depend show_depend

all: depend aconnectlib ahttplib handler_python module_authbasic ahttpserver 

aconnectlib: $(OUT_DIR)$(ACONNECT_LIB_NAME)
ahttplib: $(OUT_DIR)$(AHTTP_LIB_NAME)
handler_python: $(OUT_DIR)$(HANDLER_PYTHON_NAME) aconnectlib ahttplib
module_authbasic: $(OUT_DIR)$(MOD_BASIC_AUTH_NAME) aconnectlib ahttplib
ahttpserver: $(OUT_DIR)$(SERVER_EXE_NAME) aconnectlib ahttplib $(AHTTPSERVER_DIR)constants.hpp
depend: $(DEPENDENCIES)

show_depend:
	@echo ${DEPENDENCIES}

$(OUT_DIR)$(ACONNECT_LIB_NAME): $(ACONNECT_LIB_OBJS)
	ar ru $(OUT_DIR)$(ACONNECT_LIB_NAME) $^
	ranlib $(OUT_DIR)$(ACONNECT_LIB_NAME)

$(OUT_DIR)$(AHTTP_LIB_NAME): $(AHTTP_LIB_OBJS)
	ar ru $(OUT_DIR)$(AHTTP_LIB_NAME) $^
	ranlib $(OUT_DIR)$(AHTTP_LIB_NAME)

$(OUT_DIR)$(HANDLER_PYTHON_NAME): $(PY_HND_OBJS_FULL)  $(ACONNECT_LIB_OBJS) $(AHTTP_LIB_OBJS)
	$(CXX) $(CXXFLAGS) -shared $(LDFLAGS) $^ $(OUTPUT_OPTION)

$(OUT_DIR)$(MOD_BASIC_AUTH_NAME): $(MOD_BASIC_AUTH_OBJS_FULL) $(ACONNECT_LIB_OBJS) $(AHTTP_LIB_OBJS)
	$(CXX) $(CXXFLAGS) -shared $(LDFLAGS) $^ $(OUTPUT_OPTION)

$(OUT_DIR)$(SERVER_EXE_NAME): $(AHTTPSERVER_DIR)ahttpserver.cpp  $(ACONNECT_LIB_OBJS) $(AHTTP_LIB_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ $(OUTPUT_OPTION)

	
${ACONNECT_LIB_BUILD_DIR}%.o: ${ACONNECT_SRC_DIR}%.cpp $(ACONNECT_LIB_BUILD_DIR)%.d
	$(COMPILE.cpp) $(OUTPUT_OPTION) $<
${AHTTP_LIB_BUILD_DIR}%.o: ${AHHTP_SRC_DIR}%.cpp $(AHTTP_LIB_BUILD_DIR)%.d
	$(COMPILE.cpp) $(OUTPUT_OPTION) $<
${AHTTP_LIB_BUILD_DIR}%.o: ${TINYXML_SRC_DIR}%.cpp $(AHTTP_LIB_BUILD_DIR)%.d
	$(COMPILE.cpp) $(OUTPUT_OPTION) $<

${PY_HND_BUILD_DIR}%.o: ${HANDLER_PYTHON_DIR}%.cpp
	$(COMPILE.cpp) $(OUTPUT_OPTION) $<

${MOD_BASIC_AUTH_BUILD_DIR}%.o: ${MOD_BASIC_AUTH_DIR}%.cpp
	$(COMPILE.cpp) $(OUTPUT_OPTION) $<

#****************************************************************************
# Generate dependencies of .ccp files on .hpp files
#****************************************************************************
include $(DEPENDENCIES)

${ACONNECT_LIB_BUILD_DIR}%.d: ${ACONNECT_SRC_DIR}%.cpp
	$(CXX) -M $(CXXFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,${AHTTP_LIB_BUILD_DIR}\1.o : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$
${AHTTP_LIB_BUILD_DIR}%.d: ${AHHTP_SRC_DIR}%.cpp
	$(CXX) -M $(CXXFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,${AHTTP_LIB_BUILD_DIR}\1.o : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$
${AHTTP_LIB_BUILD_DIR}%.d: ${TINYXML_SRC_DIR}%.cpp
	$(CXX) -M $(CXXFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,${AHTTP_LIB_BUILD_DIR}\1.o : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

#****************************************************************************
# common rules
#****************************************************************************

clean:
	-rm -f $(ACONNECT_LIB_DIR)$(RELEASE_BUILD_DIR)*
	-rm -f $(ACONNECT_LIB_DIR)$(DEBUG_BUILD_DIR)*
	-rm -f $(AHTTP_LIB_DIR)$(RELEASE_BUILD_DIR)*
	-rm -f $(AHTTP_LIB_DIR)$(DEBUG_BUILD_DIR)*
	-rm -f $(OUT_DIR)$(ACONNECT_LIB_NAME)
	-rm -f $(OUT_DIR)$(AHTTP_LIB_NAME)
	










