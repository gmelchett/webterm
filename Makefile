EMCC = emcc
NATIVE_CC = cc

CFLAGS = -O3 -s WASM=1 -s MODULARIZE=1 -s EXPORT_NAME=WebTermModule -s SINGLE_FILE=1 -s EXPORTED_FUNCTIONS='["_shell_init","_shell_execute","_shell_get_prompt","_main"]' -s EXPORTED_RUNTIME_METHODS='["cwrap","UTF8ToString"]' -s ALLOW_MEMORY_GROWTH=1 -s INITIAL_MEMORY=1048576 -s MAXIMUM_MEMORY=10485760 -s ASSERTIONS=0 -s ENVIRONMENT='web'

TARGET_JS = shell.js
TARGET_HTML = index.html
TEMPLATE_HTML = index.template.html
NATIVE_BIN = wsh

.PHONY: all native clean serve

all: $(TARGET_JS) $(TARGET_HTML) native

$(TARGET_JS): shell.c
	$(EMCC) $(CFLAGS) -o $(TARGET_JS) shell.c

$(TARGET_HTML): $(TARGET_JS) $(TEMPLATE_HTML)
	python3 -c "import re,sys; \
	html=open('$(TEMPLATE_HTML)').read(); \
	js=open('$(TARGET_JS)').read(); \
	html=html.replace('    <script src=\"shell.js\"></script>\n', '    <script>\n'+js+'\n    </script>\n'); \
	open('$(TARGET_HTML)','w').write(html)"
	@echo "Built self-contained $(TARGET_HTML) (no external files needed)"

native: $(NATIVE_BIN)

$(NATIVE_BIN): shell.c
	$(NATIVE_CC) -O2 -Wall -o $(NATIVE_BIN) shell.c
	@echo "Built native executable $(NATIVE_BIN) -- run with ./$(NATIVE_BIN)"

clean:
	rm -f $(TARGET_JS) $(TARGET_HTML) $(NATIVE_BIN) shell.worker.js

serve: all
	@echo "Serving on http://localhost:8080  (or just open index.html directly)"
	python3 -m http.server 8080
