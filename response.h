extern int response_data_free(); 

extern void *
install_response_handler(CManager cm, int stone_id, char *response_spec, 
			 void *local_data);

extern int
response_determination(CManager cm, stone_type stone, event_item *event);

