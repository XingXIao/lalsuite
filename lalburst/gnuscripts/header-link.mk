all-local: header-links
header-links:
	@for file in $(lalburstinclude_HEADERS) ; do \
		sourcedir=`cd $(srcdir) && pwd`; \
		targetdir=`cd $(top_builddir)/include/lal && pwd`; \
		if test ! -r $$targetdir/$$file ; then \
			rm -f $$targetdir/$$file; \
			$(LN_S) $$sourcedir/$$file $$targetdir; \
		fi; \
	done
	@for file in LALBurstConfig.h LALBurstVCSInfo.h ; do \
		d=`pwd`; \
			targetdir=`cd $(top_builddir)/include/lal && pwd`; \
			if test ! -r $$targetdir/$$file ; then \
				rm -f $$targetdir/$$file; \
				$(LN_S) $$d/$$file $$targetdir; \
			fi; \
	done
