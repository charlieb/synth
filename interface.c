#include <gtk/gtk.h>
#include <pthread.h>
#include <math.h>
#include <stdio.h>

#include "synth.h"

typedef struct freq_adjustment {
	GtkAdjustment *adj;
	GtkLabel *label;
	int mod_id;
} freq_adjustment;

float logmap(float min_in, float max_in, float min_out, float max_out, float value) {
	float unit_val = (value - min_in) / (max_in - min_in);
	float min_log10 = log10(min_out);
	float max_log10 = log10(max_out - min_out);
	return pow(10, min_log10 + unit_val*(max_log10 - min_log10));
}

// HFO or any audio range value e.g. VCF input
static void high_freq_scale_set_value(GApplication *app, gpointer data) {
	static char label_text[10] = {0,};
	freq_adjustment *adj = data;
	float hz = logmap(0., 1., 200, 10000., gtk_adjustment_get_value(adj->adj));
	printf("HFO %i %f -> %f\n", adj->mod_id, gtk_adjustment_get_value(adj->adj), hz);
	
	sprintf(label_text, "%.2f Hz", hz);
	gtk_label_set_text(adj->label, label_text);
	set_mod_cst_value(adj->mod_id, hz);
}

GtkWidget *high_freq_scale(int mod_id) {
	printf("HFO -> %i\n", mod_id);
	GtkBox *box = (GtkBox*)gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

	GtkLabel *label = (GtkLabel*) gtk_label_new("880 Hz");
	gtk_box_pack_end(box, (GtkWidget*)label, 0, 1, 2);
	
	GtkAdjustment *adj = gtk_adjustment_new(0.25, 0., 1., 0.01, 0.01, 0.01);
	GtkWidget *scl = gtk_spin_button_new(adj, 1, 5);
	gtk_box_pack_end(box, scl, 1, 1, 2);

	freq_adjustment *freq_adj = malloc(sizeof(freq_adjustment));
	freq_adj->adj = adj;
	freq_adj->label = label;
	freq_adj->mod_id = mod_id;
	high_freq_scale_set_value(NULL, freq_adj);

	g_signal_connect(adj, "value-changed", G_CALLBACK(high_freq_scale_set_value), freq_adj);
	return (GtkWidget*)box;
}

// LFO
static void low_freq_scale_set_value(GApplication *app, gpointer data) {
	static char label_text[10] = {0,};
	freq_adjustment *adj = data;
	float hz = gtk_adjustment_get_value(adj->adj);
	printf("LFO %f -> %f\n", gtk_adjustment_get_value(adj->adj), hz);
	
	sprintf(label_text, "%.2f Hz", hz);
	gtk_label_set_text(adj->label, label_text);
	set_mod_cst_value(adj->mod_id, hz);
}

GtkWidget *low_freq_scale(int mod_id) {
	printf("LFO -> %i\n", mod_id);
	GtkBox *box = (GtkBox*)gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

	GtkLabel *label = (GtkLabel*) gtk_label_new("880 Hz");
	gtk_box_pack_end(box, (GtkWidget*)label, 0, 1, 2);
	
	GtkAdjustment *adj = gtk_adjustment_new(1., 1., 200., 0.1, 0.1, 0.1);
	GtkWidget *scl = gtk_spin_button_new(adj, 1, 5);
	gtk_box_pack_end(box, scl, 1, 1, 2);

	freq_adjustment *freq_adj = malloc(sizeof(freq_adjustment));
	freq_adj->adj = adj;
	freq_adj->label = label;
	freq_adj->mod_id = mod_id;
	low_freq_scale_set_value(NULL, freq_adj);

	g_signal_connect(adj, "value-changed", G_CALLBACK(low_freq_scale_set_value), freq_adj);
	gtk_adjustment_set_value(adj, get_mod_cst_init_value(mod_id));
	return (GtkWidget*)box;
}

// Attenuator - scale from 0 - 100%
static void percentage_scale_set_value(GApplication *app, gpointer data) {
	static char label_text[10] = {0,};
	freq_adjustment *adj = data;
	float pc = gtk_adjustment_get_value(adj->adj);
	printf("ATT %f -> %f\n", gtk_adjustment_get_value(adj->adj), pc);
	
	sprintf(label_text, "%.2f%%", pc);
	gtk_label_set_text(adj->label, label_text);
	set_mod_cst_value(adj->mod_id, pc / 100.);
}

GtkWidget *percentage_scale(int mod_id) {
	printf("ATT -> %i\n", mod_id);
	GtkBox *box = (GtkBox*)gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

	GtkLabel *label = (GtkLabel*) gtk_label_new("100%");
	gtk_box_pack_end(box, (GtkWidget*)label, 0, 1, 2);
	
	GtkAdjustment *adj = gtk_adjustment_new(50., 0., 100., 1.0, 1.0, 1.0);
	GtkWidget *scl = gtk_spin_button_new(adj, 1, 4);
	gtk_box_pack_end(box, scl, 1, 1, 2);

	freq_adjustment *freq_adj = malloc(sizeof(freq_adjustment));
	freq_adj->adj = adj;
	freq_adj->label = label;
	freq_adj->mod_id = mod_id;
	percentage_scale_set_value(NULL, freq_adj);

	g_signal_connect(adj, "value-changed", G_CALLBACK(percentage_scale_set_value), freq_adj);
	gtk_adjustment_set_value(adj, get_mod_cst_init_value(mod_id) * 100.);
	return (GtkWidget*)box;
}

/*********************************************************/
static void on_app_activate(GApplication *app, gpointer data) {
  GtkWidget *window = gtk_application_window_new(GTK_APPLICATION(app));

	GtkBox *vbox = (GtkBox*)gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	gtk_container_add(GTK_CONTAINER(window), (GtkWidget*)vbox);

	for(int i = 0; i < get_nmods(); i++) {
		if(strncmp(get_mod_type(i), "CST", 3)) continue;
		if(0 == strncmp(get_mod_cst_type(i), "NDS", 3)) continue;

		gtk_box_pack_start(vbox, gtk_label_new(get_mod_cst_label(i)), 0, 1, 2);
		if(0 == strncmp(get_mod_cst_type(i), "HFO", 3))
			gtk_box_pack_start(vbox, high_freq_scale(i), 0, 1, 2);
		else if(0 == strncmp(get_mod_cst_type(i), "LFO", 3))
			gtk_box_pack_start(vbox, low_freq_scale(i), 0, 1, 2);
		else if(0 == strncmp(get_mod_cst_type(i), "PER", 3))
			gtk_box_pack_start(vbox, percentage_scale(i), 0, 1, 2);
		else
			printf("Invalid type of CST: %c%c%c",
				 get_mod_cst_type(i)[0],
				 get_mod_cst_type(i)[1],
				 get_mod_cst_type(i)[2]);
	}

	gtk_widget_show_all(GTK_WIDGET(window));
}

int main (int argc, char *argv[]) {
	// Start the synth
	pthread_t synth_thread;

	synth_thread_data *synth = malloc(sizeof(synth_thread_data));
	pthread_mutex_init(&synth->alive_mtx, NULL);
	pthread_create(&synth_thread, NULL, synth_main_loop, (void*)synth);

	// Wait for the synth thread to startup
	int synth_alive = 0;
	while(!synth_alive) {
		pthread_mutex_lock(&synth->alive_mtx);
		synth_alive = synth->alive;
		pthread_mutex_unlock(&synth->alive_mtx);
	}

	// Create a new application
	GtkApplication *app = gtk_application_new ("com.example.GtkApplication",
			G_APPLICATION_FLAGS_NONE);
	g_signal_connect(app, "activate", G_CALLBACK (on_app_activate), NULL);
	int app_ret = g_application_run (G_APPLICATION (app), argc, argv);

	printf("Closing\n");
	pthread_mutex_lock(&synth->alive_mtx);
	synth->alive = 0;
	pthread_mutex_unlock(&synth->alive_mtx);

	pthread_join(synth_thread, NULL);

	g_object_unref(app);

	return app_ret;
}

