DIR := nucleus

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
nucleus.cpp \
worker.cpp \
workerDescriptor.cpp \
scriptExec.cpp \
urlRequest.cpp \
recoveryLog.cpp \
baseEvent.cpp \
baseQueue.cpp \
optionsNucleus.cpp \
queueContainer.cpp \
straightQueue.cpp \
workerPool.cpp \
collectionQueue.cpp \
collectionPool.cpp \
queueManagementEvent.cpp \
network.cpp \
}

# Each subdirectory must supply rules for building sources it contributes
$(DIR)/%.o: $(LIBROOT)/$(DIR)/%.cpp
		$(CC) $(CC_FLAGS) -o $@ $<

