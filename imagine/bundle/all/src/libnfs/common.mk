ifndef CHOST
 CHOST := $(shell $(CC) -dumpmachine)
endif

libnfsVer := 1.6.0
libnfsSrcDir := $(tempDir)/libnfs-$(libnfsVer)
libnfsSrcArchive := libnfs-$(libnfsVer).tar.gz

makeFile := $(buildDir)/Makefile
outputLibFile := $(buildDir)/lib/.libs/libnfs.a
installIncludeDir := $(installDir)/include/nfs

includeDirs := -I$(libnfsSrcDir)/mount -I$(libnfsSrcDir)/nfs -I$(libnfsSrcDir)/nlm -I$(libnfsSrcDir)/portmap -I$(libnfsSrcDir)/rquota

all : $(outputLibFile)

install : $(outputLibFile)
	@echo "Installing libnfs to: $(installDir)"
	@mkdir -p $(installIncludeDir) $(installDir)/lib/pkgconfig
	cp $(outputLibFile) $(installDir)/lib/
	cp $(libnfsSrcDir)/include/nfsc/*.h $(installIncludeDir)/
	cp $(libnfsSrcDir)/mount/*.h $(installIncludeDir)/
	cp $(libnfsSrcDir)/nfs/*.h $(installIncludeDir)/
	cp $(libnfsSrcDir)/nlm/*.h $(installIncludeDir)/
	cp $(libnfsSrcDir)/portmap/*.h $(installIncludeDir)/
	cp $(libnfsSrcDir)/rquota/*.h $(installIncludeDir)/
#	cp $(buildDir)/include/nfsc/*.h $(installIncludeDir)/
	cp $(buildDir)/libnfs.pc $(installDir)/lib/pkgconfig/

.PHONY : all install

$(libnfsSrcDir)/configure : | $(libnfsSrcArchive)
	@echo "Extracting libnfs..."
	@mkdir -p $(libnfsSrcDir)
	tar -mxzf $| -C $(libnfsSrcDir)/..
	cp ../gnuconfig/config.* $(libnfsSrcDir)

$(outputLibFile) : $(makeFile)
	@echo "Building libnfs..."
	 $(MAKE) -C $(<D)

$(makeFile) : $(libnfsSrcDir)/configure
	@echo "Configuring libnfs..."
	@mkdir -p $(@D)
	cd $(libnfsSrcDir); ./bootstrap
	dir=`pwd` && cd $(@D) && CC="$(CC)" CFLAGS="$(CPPFLAGS) $(CFLAGS) $(includeDirs)" LD="$(LD)" LDFLAGS="$(LDFLAGS) $(LDLIBS)" \
	$(libnfsSrcDir)/configure --prefix='$${pcfiledir}/../..' --disable-shared --host=$(CHOST) $(buildArg)

