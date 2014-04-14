DIR := exception

# Each subdirectory must contribute its source files here
C_SRCS += \
${addprefix $(ROOT)/$(DIR)/,\
}

CC_SRCS += \
${addprefix $(ROOT)/$(DIR)/,\
}

CXX_SRCS += \
${addprefix $(ROOT)/$(DIR)/,\
}

CAPC_SRCS += \
${addprefix $(ROOT)/$(DIR)/,\
}

CPP_SRCS += \
${addprefix $(ROOT)/$(DIR)/,\
Exception.cpp \
}

# Each subdirectory must supply rules for building sources it contributes
$(DIR)/%.o: $(LIBROOT)/$(DIR)/%.cpp
		$(CC) $(CC_FLAGS) -o $@ $<

