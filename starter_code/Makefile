CC = gcc -c
SHELL = /bin/bash

# compiling flags here
CFLAGS = -Wall -I.

LINKER = gcc -o
# linking flags here
LFLAGS = -Wall

OBJDIR = ../objects
SENDER_OBJDIR = $(OBJDIR)/sender
RECEIVER_OBJDIR = $(OBJDIR)/receiver

SENDER_OBJECTS := $(SENDER_OBJDIR)/rdt_sender.o $(SENDER_OBJDIR)/common.o $(SENDER_OBJDIR)/packet.o
RECEIVER_OBJECTS := $(RECEIVER_OBJDIR)/rdt_receiver.o $(RECEIVER_OBJDIR)/common.o $(RECEIVER_OBJDIR)/packet.o

# Program name
SENDER := $(SENDER_OBJDIR)/rdt_sender
RECEIVER := $(RECEIVER_OBJDIR)/rdt_receiver

rm = rm -f
rmdir = rmdir

TARGET: $(SENDER_OBJDIR) $(RECEIVER_OBJDIR) $(SENDER) $(RECEIVER)

$(SENDER): $(SENDER_OBJECTS)
	$(LINKER) $@ $(SENDER_OBJECTS)
	@echo "Sender link complete!"

$(RECEIVER): $(RECEIVER_OBJECTS)
	$(LINKER) $@ $(RECEIVER_OBJECTS)
	@echo "Receiver link complete!"

$(SENDER_OBJDIR)/%.o: %.c common.h packet.h
	$(CC) $(CFLAGS) $< -o $@
	@echo "Sender compilation complete!"

$(RECEIVER_OBJDIR)/%.o: %.c common.h packet.h
	$(CC) $(CFLAGS) $< -o $@
	@echo "Receiver compilation complete!"

clean:
	@if [ -d $(OBJDIR) ]; then rm -r $(OBJDIR); fi;
	@echo "Cleanup complete!"

$(SENDER_OBJDIR):
	@[ -d $(SENDER_OBJDIR) ] || mkdir -p $(SENDER_OBJDIR)

$(RECEIVER_OBJDIR):
	@[ -d $(RECEIVER_OBJDIR) ] || mkdir -p $(RECEIVER_OBJDIR)
