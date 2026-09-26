/* Extracting parameters to a CSV file (API.md): one row per 10 ms frame, in Hz and dB, written to hello.csv.
 *
 *   params_export [v11] [TEXT] */
#include "prose.h"

#include <stdio.h>
#include <string.h>

static void on_params(prose_h h, const uint8_t *p, uint32_t pos, void *user)
{
	FILE *f = user;
	fprintf(f, "%lu", (unsigned long)(pos / 10)); /* time in ms: 10 samples per ms */
	for (int i = 0; i < prose_param_count(h); i++)
		fprintf(f, ",%g", prose_param_value(h, i, p[i])); /* Hz or dB */
	fputc('\n', f);
}

static int discard_audio(prose_h h, const int16_t *pcm, size_t count, uint32_t pos, void *user)
{
	(void)h;
	(void)pcm;
	(void)count;
	(void)pos;
	(void)user;
	return 0;
}

int main(int argc, char **argv)
{
	int a = 1, version = PROSE_V341;
	if (argc > a && !strcmp(argv[a], "v11")) {
		version = PROSE_V11;
		a++;
	}
	const char *text = argc > a ? argv[a] : "Hello there.";
	prose_h h;
	if (prose_open(&h, version))
		return 1;
	FILE *f = fopen("hello.csv", "w");
	if (!f)
		return 1;
	fprintf(f, "time_ms");
	for (int i = 0; i < prose_param_count(h); i++)
		fprintf(f, ",%s", prose_param_name(h, i)); /* AV,AF,AH,A2,...,F1,F2,...,F0 */
	fputc('\n', f);
	prose_callbacks cb = {0};
	cb.on_params = on_params;
	prose_set_callbacks(h, &cb, f);
	int32_t total = prose_speak_to_buffer(h, text, NULL, 0, discard_audio, NULL);
	fclose(f);
	printf("hello.csv: %s (%.2f s of speech)\n", total < 0 ? prose_error_string(total) : "written",
	       total < 0 ? 0 : total / 10000.0);
	prose_close(h);
	return total < 0;
}
