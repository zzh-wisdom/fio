/**
 * dhms engine
 *
 * 采用read/write读写文件到指定的dsm_addr中
 *
 */
#include <dhms/dhms.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "../fio.h"
#include "../optgroup.h"
#include "../verify.h"

struct dhms_options_values {
  void *pad;
  char *conf_file;
};

static struct fio_option options[] = {
    {
        .name = "conf",
        .lname = "configure file",
        .type = FIO_OPT_STR_STORE,
        .off1 = offsetof(struct dhms_options_values, conf_file),
        .help = "Client's configure file",
        .category = FIO_OPT_C_ENGINE,
        .group = FIO_OPT_G_DHMS,
    },
    {
        .name = NULL,
    }};

struct dhms_data {
  size_t ws_size;  // 数据集大小
  size_t bs;  // 单次io的大小
  int obj_hdl_num;
  dhms_epoch_t epoch;

  // 全局client启动后才初始化
  dhms_connect_handle_t ch;
  dhms_pool_handle_t ph;
  dhms_obj_handle_t *obj_hdls;
};

const char* client_ip = "192.168.1.33";
const char* master_ip = "192.168.1.33";
static pthread_mutex_t g_init_mut;
static int thread_total_num = 0;
static pthread_barrier_t g_thread_sync_barrier;
static bool has_initiator = false; // 是否有线程在初始化

#define MAX_BS 4096
char fio_load_buf[MAX_BS] = "fio: obj init data";
#define MAX_THREAD_NUM 128
dhms_pool_handle_t g_pool_handles[MAX_THREAD_NUM];
dhms_pool_handle_t* g_obj_hdls[MAX_THREAD_NUM];


// 忽略
static int fio_dhms_setup(struct thread_data *td) {
  fprintf(stderr, "[%s] \n", __func__);
  return 0;
}

// 初始化全局环境
static int fio_dhms_init(struct thread_data *td) {
  struct dhms_data *dd;
  struct thread_options *o = &td->o;
  // struct dhms_options_values *eo = td->eo;
  int rc;
  dhms_client_options_t opts;
  bool i_am_initiator = false;
  int qp_id;
  char pool_name[512];
  uint64_t pool_size;

  printf("thread_total_num: %u\n", td->thread_total_num);

  dd = td->io_ops_data;
  dd = malloc(sizeof(*dd));
  assert(dd);
  memset(dd, 0, sizeof(*dd));
  dd->ws_size = o->size;
  dd->bs = o->bs[DDIR_WRITE];
  assert(dd->bs);
  assert(dd->bs <= MAX_BS);
  dd->obj_hdl_num = dd->ws_size / dd->bs;
  assert(dd->obj_hdl_num);
  dd->epoch = 1;
  td->io_ops_data = dd;

  // 竞争初始化者
  pthread_mutex_lock(&g_init_mut);
  if(!has_initiator) {
    i_am_initiator = true;
    has_initiator = true;
  }
  pthread_mutex_unlock(&g_init_mut);

  if(i_am_initiator) {
    // 初始化全局环境，即各个线程共享的部分，包括
    // 1. 全局共享变量
    // 2. 共享dhms环境（如客户端代理，master qp）
    // 3. 创建pool（master qp目前非多线程安全）
    // 4. oid预分配
    thread_total_num = td->thread_total_num;
    rc = pthread_barrier_init(&g_thread_sync_barrier, NULL, thread_total_num);
    assert(!rc);
    opts.client_port = 10070,
    opts.master_port = 10086,
    opts.dev_id = 0,
    opts.port_id = 1,
    opts.io_size = dd->bs,
    opts.msg_ring_buffer_tail_size = 0,
    strcpy(opts.client_ip, client_ip);
    strcpy(opts.master_ip, master_ip);
    rc = dhms_client_init(&opts);
    assert(!rc);
    for(int i = 0; i < td->thread_total_num; ++i) {
      snprintf(pool_name, 512, "%s:[%d]", "fio_thread_pool", i);
      // 创建pool
      pool_size = (dd->obj_hdl_num >> HOS_REGION_OBJ_ID_BITS)*REGION_MAX_SIZE;
      if(pool_size < dd->ws_size) pool_size = dd->ws_size;
      dhms_pool_create(pool_name, pool_size, &g_pool_handles[i]);
      // OID 预分配
      g_obj_hdls[i] = malloc(dd->obj_hdl_num * sizeof(dhms_obj_handle_t));
      assert(g_obj_hdls[i]);
      for(int j = 0; j < dd->obj_hdl_num; ++j) {
        rc = dhms_obj_alloc(g_pool_handles[i], dd->bs, &g_obj_hdls[i][j]);
        assert(!rc);
      }
    }
  }
  pthread_barrier_wait(&g_thread_sync_barrier);

  // 每个线程和dn建立qp
  qp_id = td->thread_number - 1;
  assert(qp_id >= 0);
  dhms_connect_handle_create(qp_id, false, &dd->ch);
  dd->ph = g_pool_handles[td->thread_number - 1];
  dd->obj_hdls = g_obj_hdls[td->thread_number - 1];

  printf("\tobj_hdl_num: %d\n", dd->obj_hdl_num);
  for(int i = 0; i < dd->obj_hdl_num; ++i) {
    rc = dhms_obj_write_sync(dd->ph, dd->obj_hdls[i], dd->epoch, 0, dd->bs,fio_load_buf, dd->ch);
    assert(rc == dd->bs);
  }
  printf("Load Complete [thread: %d]\n", td->thread_number);

  pthread_barrier_wait(&g_thread_sync_barrier);
  return 0;
}

int fio_dhms_post_init(struct thread_data *td) {
  fprintf(stderr, "[%s] \n", __func__);
  return 0;
}

int fio_dhms_get_file_size(struct thread_data *td, struct fio_file *f) {
  struct dhms_data *dd = td->io_ops_data;
  fprintf(stderr, "[%s] \n", __func__);

  f->real_file_size = dd->ws_size;
  fio_file_set_size_known(f);

  return 0;
}

static enum fio_q_status fio_dhms_queue(struct thread_data *td,
                                        struct io_u *io_u) {
  struct dhms_data *dd = td->io_ops_data;
  int ret = 0;
  int idx = io_u->offset / dd->bs % dd->obj_hdl_num;

  if (io_u->ddir == DDIR_READ) {
    // ret = io_u->xfer_buflen;
    assert(dd->bs == io_u->xfer_buflen);
    ret = dhms_obj_read_sync(dd->ph, dd->obj_hdls[idx], dd->epoch, dd->bs, io_u->xfer_buf, 0, dd->ch);
    // assert(!ret);
  } else if (io_u->ddir == DDIR_WRITE) {
    // ret = io_u->xfer_buflen;
    assert(dd->bs == io_u->xfer_buflen);
    ret = dhms_obj_write_sync(dd->ph, dd->obj_hdls[idx], dd->epoch++, 0, dd->bs, io_u->xfer_buf, dd->ch);
    // assert(!ret);
  } else {
    log_err("dhms: Invalid I/O Operation: %d\n", io_u->ddir);
    ret = EINVAL;
  }
  if (ret != (int)io_u->xfer_buflen) {
    if (ret >= 0) {
      io_u->resid = io_u->xfer_buflen - ret;
      io_u->error = 0;
      return FIO_Q_COMPLETED;
    } else {
      io_u->error = errno;
    }
  }
  if (io_u->error) td_verror(td, io_u->error, "xfer");
  return FIO_Q_COMPLETED;
}

static int fio_dhms_open_file(struct thread_data *td, struct fio_file *f) {
  fprintf(stderr, "[%s] \n", __func__);
  return 0;
}

static int fio_dhms_close_file(struct thread_data *td, struct fio_file *f) {
  fprintf(stderr, "[%s] \n", __func__);
  return 0;
}

static struct ioengine_ops ioengine = {
    .name = "dhms",
    .version = FIO_IOOPS_VERSION,
    .setup = fio_dhms_setup,
    .init = fio_dhms_init,
    .post_init = fio_dhms_post_init,
    .get_file_size = fio_dhms_get_file_size,
    .queue = fio_dhms_queue,
    .open_file = fio_dhms_open_file,
    .close_file = fio_dhms_close_file,
    .flags = FIO_DISKLESSIO | FIO_SYNCIO,
    .options = options,
    .option_struct_size = sizeof(struct dhms_options_values),
};

static void fio_init fio_dhms_register(void) {
  register_ioengine(&ioengine);
  pthread_mutex_init(&g_init_mut, NULL);
}

static void fio_exit fio_dhms_unregister(void) {
  unregister_ioengine(&ioengine);
  pthread_mutex_destroy(&g_init_mut);
}