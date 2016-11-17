dist_bin_SCRIPTS = aqnet aqs ckey diffprof getprof git-diffuniq \
		   prdiff razor.py rifle.py

bin_SCRIPTS = diffuniq razor rifle trifle xmlnm

diffuniq:
	$(LN_S) git-diffuniq diffuniq

razor:
	$(LN_S) razor.py razor

rifle:
	$(LN_S) rifle.py rifle

trifle:
	$(LN_S) ../trifle/trifle trifle

xmlnm:
	$(LN_S) rifle xmlnm

install-exec-hook:
	rm -f $(DESTDIR)$(bindir)/diffuniq \
	      $(DESTDIR)$(bindir)/razor \
	      $(DESTDIR)$(bindir)/rifle \
	      $(DESTDIR)$(bindir)/xmlnm
	$(LN_S) git-diffuniq $(DESTDIR)$(bindir)/diffuniq
	$(LN_S) razor.py $(DESTDIR)$(bindir)/razor
	$(LN_S) rifle.py $(DESTDIR)$(bindir)/rifle
	$(LN_S) rifle $(DESTDIR)$(bindir)/xmlnm

clean-local:
	rm -f *.pyc *.pyo
	rm -f diffuniq razor rifle trifle xmlnm
