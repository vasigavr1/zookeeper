#include "zk_util.h"



void zk_stats(stats_ctx_t *ctx)
{
  double seconds = ctx-> seconds;
  uint16_t print_count = ctx->print_count;
  t_stats_t *curr_w_stats = ctx->curr_w_stats;
  t_stats_t *prev_w_stats = ctx->prev_w_stats;
  all_stats_t all_stats;


  double total_throughput = 0;
  uint64_t all_clients_cache_hits = 0;
  seconds *= MILLION; // compute only MIOPS
  for (int i = 0; i < num_threads; i++) {
    all_clients_cache_hits += curr_w_stats[i].cache_hits_per_thread - prev_w_stats[i].cache_hits_per_thread;
    all_stats.cache_hits_per_thread[i] =
      (curr_w_stats[i].cache_hits_per_thread - prev_w_stats[i].cache_hits_per_thread) / seconds;

    all_stats.stalled_gid[i] = (curr_w_stats[i].stalled_gid - prev_w_stats[i].stalled_gid) / seconds;
    all_stats.stalled_ack_prep[i] = (curr_w_stats[i].stalled_ack_prep - prev_w_stats[i].stalled_ack_prep) / seconds;
    all_stats.stalled_com_credit[i] =
      (curr_w_stats[i].stalled_com_credit - prev_w_stats[i].stalled_com_credit) / seconds;

    all_stats.preps_sent[i] = (curr_w_stats[i].preps_sent - prev_w_stats[i].preps_sent) / seconds;
    all_stats.coms_sent[i] = (curr_w_stats[i].coms_sent - prev_w_stats[i].coms_sent) / seconds;
    all_stats.acks_sent[i] = (curr_w_stats[i].acks_sent - prev_w_stats[i].acks_sent) / seconds;
    all_stats.received_coms[i] = (curr_w_stats[i].received_coms - prev_w_stats[i].received_coms) / seconds;
    all_stats.received_preps[i] = (curr_w_stats[i].received_preps - prev_w_stats[i].received_preps) / seconds;
    all_stats.received_acks[i] = (curr_w_stats[i].received_acks - prev_w_stats[i].received_acks) / seconds;
    if (machine_id == LEADER_MACHINE) {
      all_stats.batch_size_per_thread[i] = (curr_w_stats[i].total_writes - prev_w_stats[i].total_writes) /
                                           (double) (curr_w_stats[i].batches_per_thread -
                                                     prev_w_stats[i].batches_per_thread);
      all_stats.com_batch_size[i] = (curr_w_stats[i].coms_sent - prev_w_stats[i].coms_sent) /
                                    (double) (curr_w_stats[i].coms_sent_mes_num -
                                              prev_w_stats[i].coms_sent_mes_num);
      all_stats.prep_batch_size[i] = (curr_w_stats[i].preps_sent - prev_w_stats[i].preps_sent) /
                                     (double) (curr_w_stats[i].prep_sent_mes_num -
                                               prev_w_stats[i].prep_sent_mes_num);
    }
    else {
      all_stats.ack_batch_size[i] = (curr_w_stats[i].acks_sent - prev_w_stats[i].acks_sent) /
                                    (double) (curr_w_stats[i].acks_sent_mes_num -
                                              prev_w_stats[i].acks_sent_mes_num);
      all_stats.write_batch_size[i] = (curr_w_stats[i].writes_sent - prev_w_stats[i].writes_sent) /
                                      (double) (curr_w_stats[i].writes_sent_mes_num -
                                                prev_w_stats[i].writes_sent_mes_num);
    }
  }

  memcpy(prev_w_stats, curr_w_stats, num_threads * (sizeof(struct thread_stats)));
  total_throughput = (all_clients_cache_hits) / seconds;

  printf("---------------PRINT %d time elapsed %.2f---------------\n", print_count, seconds / MILLION);
  my_printf(green, "SYSTEM MIOPS: %.2f \n", total_throughput);
  for (int i = 0; i < num_threads; i++) {
    my_printf(cyan, "T%d: ", i);
    my_printf(yellow, "%.2f MIOPS, STALL: GID: %.2f/s, ACK/PREP %.2f/s, COM/CREDIT %.2f/s", i,
              all_stats.cache_hits_per_thread[i],
              all_stats.stalled_gid[i],
              all_stats.stalled_ack_prep[i],
              all_stats.stalled_com_credit[i]);
    if (machine_id == LEADER_MACHINE) {
      my_printf(yellow, ", BATCHES: GID %.2f, Coms %.2f, Preps %.2f ",
                all_stats.batch_size_per_thread[i],
                all_stats.com_batch_size[i],
                all_stats.prep_batch_size[i]);
    }
    else {
      my_printf(yellow, ", BATCHES: Acks %.2f, Ws %.2f ",
                all_stats.ack_batch_size[i],
                all_stats.write_batch_size[i]);
    }
    //if (i > 0 && i % 2 == 0)
    printf("\n");
  }
  printf("\n");
  printf("---------------------------------------\n");
  if (ENABLE_CACHE_STATS == 1)
    //print_cache_stats(start, machine_id);
    // // Write to a file all_clients_throughput, per_worker_remote_throughput[], per_worker_local_throughput[]
    if (DUMP_STATS_2_FILE == 1)
      dump_stats_2_file(&all_stats);
  my_printf(green, "SYSTEM MIOPS: %.2f \n", total_throughput);
}


