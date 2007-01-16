extern int response_data_free(); 

extern void *
install_response_handler(CManager cm, int stone_id, char *response_spec, 
			 void *local_data, IOFormat **ref_ptr);

extern int
response_determination(CManager cm, stone_type stone, action_class stage, event_item *event);

extern void
dump_mrd(void *mrd);
