/* Private versioned JSON codec for the image editor's retained annotations. */
#pragma once

typedef struct {
	char *data;
	DWORD length, capacity;
	BOOL failed;
} PIN_JSON;

static void pin_json_append(PIN_JSON *json, const char *text)
{
	size_t length = strlen(text);
	DWORD needed;
	if (json->failed) return;
	if (length > 256 * 1024 * 1024 || json->length > 256 * 1024 * 1024 - length) {
		json->failed = TRUE; return;
	}
	needed = json->length + (DWORD)length + 1;
	if (needed > json->capacity) {
		DWORD capacity = max(needed, max(1024, json->capacity * 2));
		char *data = mem_alloc(capacity);
		if (data == NULL) { json->failed = TRUE; return; }
		if (json->length) CopyMemory(data, json->data, json->length);
		mem_free((void **)&json->data);
		json->data = data; json->capacity = capacity;
	}
	CopyMemory(json->data + json->length, text, length + 1);
	json->length += (DWORD)length;
}

static void pin_json_int(PIN_JSON *json, int value)
{
	char text[24];
	_snprintf_s(text, sizeof(text), _TRUNCATE, "%d", value);
	pin_json_append(json, text);
}

static void pin_json_string(PIN_JSON *json, const TCHAR *text)
{
	char escaped[8];
	if (text == NULL) { pin_json_append(json, "null"); return; }
	pin_json_append(json, "\"");
	for (; *text && !json->failed; text++) {
		/* Escaping UTF-16 code units also preserves surrogate pairs exactly. */
		_snprintf_s(escaped, sizeof(escaped), _TRUNCATE, "\\u%04x", (unsigned int)(WORD)*text);
		pin_json_append(json, escaped);
	}
	pin_json_append(json, "\"");
}

static void pin_json_bitmap(PIN_JSON *json, HBITMAP bitmap)
{
	static const char hex[] = "0123456789abcdef";
	DWORD size = 0, i;
	BYTE *png;
	char pair[3] = {0};
	if (bitmap == NULL) { pin_json_append(json, "null"); return; }
	png = bitmap_to_png(bitmap, &size);
	if (png == NULL) { json->failed = TRUE; return; }
	pin_json_append(json, "\"");
	for (i = 0; i < size && !json->failed; i++) {
		pair[0] = hex[png[i] >> 4]; pair[1] = hex[png[i] & 15];
		pin_json_append(json, pair);
	}
	pin_json_append(json, "\"");
	mem_free((void **)&png);
}

static char *serialize_annotations(const PIN_ARTIFACT *item)
{
	PIN_JSON json = {0};
	BOOL comma = FALSE;
	int i;
	pin_json_append(&json, "{\"version\":1,\"artifacts\":[");
	for (; item != NULL && !json.failed; item = item->next) {
		const int geometry[] = {item->start.x, item->start.y, item->end.x, item->end.y,
			item->box.left, item->box.top, item->box.right, item->box.bottom};
		if (item->deleted) continue;
		if (comma) pin_json_append(&json, ",");
		comma = TRUE;
		pin_json_append(&json, "{\"tool\":"); pin_json_int(&json, item->tool);
		pin_json_append(&json, ",\"stroke\":"); pin_json_int(&json, item->stroke);
		pin_json_append(&json, ",\"color\":"); pin_json_int(&json, (int)item->color);
		pin_json_append(&json, ",\"geometry\":[");
		for (i = 0; i < ARRAYSIZE(geometry); i++) {
			if (i) pin_json_append(&json, ",");
			pin_json_int(&json, geometry[i]);
		}
		pin_json_append(&json, "],\"text_size\":"); pin_json_int(&json, item->text_size);
		pin_json_append(&json, ",\"number\":"); pin_json_int(&json, item->number);
		pin_json_append(&json, ",\"font\":"); pin_json_string(&json, item->font_face);
		pin_json_append(&json, ",\"text\":"); pin_json_string(&json, item->text);
		pin_json_append(&json, ",\"points\":[");
		for (i = 0; i < item->count; i++) {
			if (i) pin_json_append(&json, ",");
			pin_json_append(&json, "["); pin_json_int(&json, item->points[i].x);
			pin_json_append(&json, ","); pin_json_int(&json, item->points[i].y);
			pin_json_append(&json, "]");
		}
		pin_json_append(&json, "],\"magnified_png\":"); pin_json_bitmap(&json, item->magnified_source);
		pin_json_append(&json, "}");
	}
	pin_json_append(&json, "]}");
	if (json.failed) mem_free((void **)&json.data);
	return json.data;
}

static BOOL pin_json_expect(const char **input, const char *text)
{
	size_t length = strlen(text);
	while (**input == ' ' || **input == '\t' || **input == '\r' || **input == '\n') (*input)++;
	if (strncmp(*input, text, length) != 0) return FALSE;
	*input += length;
	return TRUE;
}

static BOOL pin_json_read_int(const char **input, int *value)
{
	char *end;
	long number;
	while (**input == ' ' || **input == '\t' || **input == '\r' || **input == '\n') (*input)++;
	if (**input != '-' && (**input < '0' || **input > '9')) return FALSE;
	errno = 0;
	number = strtol(*input, &end, 10);
	if (end == *input || errno == ERANGE || number < INT_MIN || number > INT_MAX) return FALSE;
	*input = end; *value = (int)number;
	return TRUE;
}

static int pin_json_hex(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static BOOL pin_json_read_string(const char **input, TCHAR **result)
{
	const char *end;
	TCHAR *text;
	int count = 0;
	*result = NULL;
	if (pin_json_expect(input, "null")) return TRUE;
	if (!pin_json_expect(input, "\"")) return FALSE;
	end = *input;
	while (*end && *end != '"') {
		if (*end == '\\') {
			end++;
			if (!*end) return FALSE;
		}
		end++;
	}
	if (*end != '"' || end - *input > 6 * 1024 * 1024) return FALSE;
	text = mem_alloc((DWORD)(end - *input + 1) * sizeof(TCHAR));
	if (text == NULL) return FALSE;
	while (*input < end) {
		unsigned int c = (BYTE)*(*input)++;
		if (c == '\\') {
			c = (BYTE)*(*input)++;
			if (c == 'u') {
				int i;
				c = 0;
				for (i = 0; i < 4; i++) {
					int digit;
					if (*input >= end || (digit = pin_json_hex(*(*input)++)) < 0) goto invalid;
					c = c * 16 + digit;
				}
				if (c == 0) goto invalid;
			} else if (c == 'n') c = '\n';
			else if (c == 'r') c = '\r';
			else if (c == 't') c = '\t';
			else if (c != '"' && c != '\\' && c != '/') goto invalid;
		} else if (c < 32 || c >= 128) goto invalid;
		text[count++] = (TCHAR)c;
	}
	text[count] = 0; (*input)++; *result = text;
	return TRUE;
invalid:
	mem_free((void **)&text); return FALSE;
}

static BOOL pin_json_read_bitmap(const char **input, HBITMAP *bitmap)
{
	const char *end;
	BYTE *png;
	DWORD size, i;
	*bitmap = NULL;
	if (pin_json_expect(input, "null")) return TRUE;
	if (!pin_json_expect(input, "\"")) return FALSE;
	end = strchr(*input, '"');
	if (end == NULL || (end - *input) % 2 || end - *input > 128 * 1024 * 1024) return FALSE;
	size = (DWORD)(end - *input) / 2;
	if (size == 0 || (png = mem_alloc(size)) == NULL) return FALSE;
	for (i = 0; i < size; i++) {
		int a = pin_json_hex((*input)[i * 2]), b = pin_json_hex((*input)[i * 2 + 1]);
		if (a < 0 || b < 0) { mem_free((void **)&png); return FALSE; }
		png[i] = (BYTE)(a * 16 + b);
	}
	*bitmap = png_to_bitmap(png, size);
	mem_free((void **)&png);
	*input = end + 1;
	return *bitmap != NULL;
}

static BOOL saved_annotation_tool(int tool)
{
	switch (tool) {
	case ID_PEN: case ID_MARKER: case ID_LINE: case ID_ARROW: case ID_FRAME_ARROW:
	case ID_RECT: case ID_FILLED_RECT: case ID_ELLIPSE: case ID_FILLED_ELLIPSE:
	case ID_TEXT: case ID_CALLOUT: case ID_DIMENSION: case ID_DIMENSION_SLASH: case ID_STEP: case ID_REDACT:
	case ID_INVERT: case ID_SPOTLIGHT: case ID_HIGHLIGHT: case ID_HIGHLIGHT_BLOCK:
	case ID_ROUGH_HIGHLIGHT: case ID_MAGNIFY: return TRUE;
	}
	return FALSE;
}

static BOOL deserialize_annotations(const char *json, PIN_ARTIFACT **result)
{
	PIN_ARTIFACT *head = NULL, **tail = &head;
	TCHAR *font = NULL;
	int count = 0;
	*result = NULL;
	if (json == NULL) return FALSE;
	if (!pin_json_expect(&json, "{\"version\":1,\"artifacts\":[")) return FALSE;
	while (!pin_json_expect(&json, "]")) {
		PIN_ARTIFACT *item;
		int geometry[8], color, i;
		if (++count > 10000 || (item = mem_calloc(sizeof(*item))) == NULL) goto invalid;
		*tail = item; tail = &item->next;
		if (!pin_json_expect(&json, "{\"tool\":") || !pin_json_read_int(&json, &item->tool) ||
			!saved_annotation_tool(item->tool) || !pin_json_expect(&json, ",\"stroke\":") ||
			!pin_json_read_int(&json, &item->stroke) || item->stroke < 1 || item->stroke > 1000000 ||
			!pin_json_expect(&json, ",\"color\":") || !pin_json_read_int(&json, &color) || color < 0 || color > 0xffffff ||
			!pin_json_expect(&json, ",\"geometry\":[")) goto invalid;
		item->color = (COLORREF)color;
		for (i = 0; i < ARRAYSIZE(geometry); i++) {
			if ((i && !pin_json_expect(&json, ",")) || !pin_json_read_int(&json, &geometry[i]) ||
				geometry[i] < -1000000 || geometry[i] > 1000000) goto invalid;
		}
		item->start = (POINT){geometry[0], geometry[1]}; item->end = (POINT){geometry[2], geometry[3]};
		item->box = (RECT){geometry[4], geometry[5], geometry[6], geometry[7]};
		if (!pin_json_expect(&json, "],\"text_size\":") || !pin_json_read_int(&json, &item->text_size) ||
			item->text_size < 0 || item->text_size > 1000000 ||
			!pin_json_expect(&json, ",\"number\":") || !pin_json_read_int(&json, &item->number) || item->number < 0 ||
			!pin_json_expect(&json, ",\"font\":") || !pin_json_read_string(&json, &font) || font == NULL ||
			lstrlen(font) >= LF_FACESIZE) goto invalid;
		lstrcpy(item->font_face, font); mem_free((void **)&font);
		if (!pin_json_expect(&json, ",\"text\":") || !pin_json_read_string(&json, &item->text) ||
			!pin_json_expect(&json, ",\"points\":[")) goto invalid;
		while (!pin_json_expect(&json, "]")) {
			POINT point;
			int x, y;
			if (item->count >= 1000000 || !pin_json_expect(&json, "[") || !pin_json_read_int(&json, &x) ||
				!pin_json_expect(&json, ",") || !pin_json_read_int(&json, &y) || !pin_json_expect(&json, "]") ||
				x < -1000000 || x > 1000000 || y < -1000000 || y > 1000000) goto invalid;
			point = (POINT){x, y};
			if (!append_artifact_point(item, point)) goto invalid;
			if (*json != ']' && !pin_json_expect(&json, ",")) goto invalid;
		}
		if (!pin_json_expect(&json, ",\"magnified_png\":") || !pin_json_read_bitmap(&json, &item->magnified_source) ||
			!pin_json_expect(&json, "}")) goto invalid;
		if (*json != ']' && !pin_json_expect(&json, ",")) goto invalid;
	}
	if (!pin_json_expect(&json, "}") || !pin_json_expect(&json, "") || *json) goto invalid;
	*result = head; return TRUE;
invalid:
	mem_free((void **)&font); free_artifacts(head); return FALSE;
}
