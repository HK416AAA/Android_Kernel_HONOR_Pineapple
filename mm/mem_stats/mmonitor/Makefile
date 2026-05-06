#
# Makefile for the honor's linux memory dmd driver.
#

mmonitor_obj-y	:=	mmonitor.o proc_tid_memstat.o allocpages_delayacct.o
obj-y += mmonitor_obj.o
obj-$(CONFIG_SLOW_PATH_COUNT) += slowpath_count.o
