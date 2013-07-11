BUILDDATE=`date +%Y.%m.%d`
REAL_VERSION=$(BUILDDATE)

BUILDDATE=`date +%Y.%m.%d`
GIT_VERSION=$(shell git log -1 --format="%cd %h" --date=short 2>/dev/null)
VERSION=$(shell if [ "$(GIT_VERSION)" ]; then echo "commit\ \"$(GIT_VERSION)\""; else echo "$(REAL_VERSION)"; fi)
NAME=$(shell if [ "$(GIT_VERSION)" ]; then echo "$(REAL_NAME)-git"; else echo "$(REAL_NAME)"; fi)
