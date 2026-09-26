/* Synthesizing from a CSV file (API.md): hello.csv (from params_export) read back one row per frame through the raw
 * path, saved as hello_csv.wav. The header decides which column sets which parameter.
 *
 *   params_import [v11] [FILE.csv] */
#include "prose.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	FILE *csv;
	int col[32]; /* parameter number of each column after time_ms, or -1 to skip it */
	int n;       /* number of those columns */
	int16_t *pcm; /* where the audio goes */
} reader;

/* load the next row into the frame parameters; 0 at the end of the file */
static int next_row(prose_h h, reader *r)
{
	double v;
	if (fscanf(r->csv, " %*f") == EOF) /* skip time_ms */
		return 0;
	for (int i = 0; i < r->n; i++)
		if (fscanf(r->csv, ",%lf", &v) == 1 && r->col[i] >= 0)
			prose_set_frame_param(h, r->col[i], prose_param_raw(h, r->col[i], v));
	return 1;
}

static int on_frame(prose_h h, int frame, const int16_t *pcm, int count, void *user)
{
	reader *r = user;
	memcpy(r->pcm + (size_t)frame * 100, pcm, count * sizeof *pcm);
	return !next_row(h, r); /* nonzero stops at the end of the file */
}

int main(int argc, char **argv)
{
	int a = 1, version = PROSE_V341;
	if (argc > a && !strcmp(argv[a], "v11")) {
		version = PROSE_V11;
		a++;
	}
	const char *file = argc > a ? argv[a] : "hello.csv";
	prose_h h;
	if (prose_open(&h, version))
		return 1;
	reader r = {fopen(file, "r"), {0}, 0, NULL};
	if (!r.csv) {
		printf("cannot open %s (run params_export first)\n", file);
		return 1;
	}
	char line[1024], *name;
	int max_frames = 0;
	while (fgets(line, sizeof line, r.csv)) /* the number of data lines bounds the frames */
		max_frames++;
	rewind(r.csv);
	if (!fgets(line, sizeof line, r.csv)) /* the header */
		return 1;
	max_frames--;
	strtok(line, ",\r\n"); /* time_ms */
	while ((name = strtok(NULL, ",\r\n")) && r.n < 32)
		r.col[r.n++] = prose_param_index(h, name);

	r.pcm = malloc((size_t)(max_frames > 0 ? max_frames : 1) * 100 * sizeof *r.pcm);
	int frames = 0;
	if (r.pcm && next_row(h, &r)) /* row 0 is the first frame */
		frames = prose_render_frames(h, max_frames, on_frame, &r);
	fclose(r.csv);
	int err = frames < 0 ? frames : prose_save_wave("hello_csv.wav", r.pcm, (size_t)frames * 100);
	printf("%d frames from %s; hello_csv.wav: %s\n", frames, file, prose_error_string(err));
	free(r.pcm);
	prose_close(h);
	return err ? 1 : 0;
}
