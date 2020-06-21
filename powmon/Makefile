
INIPARSER = ./lib/iniparser
MOSQUITTO = ./lib/mosquitto-1.3.5
POW_MON = ./bin

INSTALL_TARGET = $(POW_MON) 
SUBDIRS = $(INIPARSER) $(MOSQUITTO) $(POW_MON) $(PERFMON)

.PHONY: subdirs $(SUBDIRS) clean

subdirs: $(SUBDIRS)

clean:
	for dir in $(SUBDIRS); do \
	$(MAKE) -C $$dir clean; \
	done

install:
	for dir in $(INSTALL_TARGET); do \
	$(MAKE) -C $$dir install; \
	done

$(SUBDIRS):
	$(MAKE) -C $@

$(POW_MON): $(INIPARSER) $(MOSQUITTO) $(PERFMON)

