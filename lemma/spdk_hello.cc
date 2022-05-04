#include "lemma/spdk_hello.h"

SpdkHello::SpdkHello(){
	int rc;
	struct spdk_env_opts opts;

	spdk_env_opts_init(&opts);
	opts.name = "hello_world";
	opts.shm_id = 0;
	if (spdk_env_init(&opts) < 0) {
		fprintf(stderr, "Unable to initialize SPDK env\n");
	}

	printf("Initializing NVMe Controllers\n");

	rc = spdk_nvme_probe(NULL, NULL, probe_cb, attach_cb, NULL);
	if (rc != 0) {
		fprintf(stderr, "spdk_nvme_probe() failed\n");
		cleanup();
	}

	if (_g_controllers == nullptr) {
		fprintf(stderr, "no NVMe controllers found\n");
		cleanup();
	}
	fprintf(stderr, "Initialization complete.\n");
}


void SpdkHello::read_complete(void *arg, const struct spdk_nvme_cpl *completion)
{
	struct_hello_world_sequence *sequence = (struct_hello_world_sequence*)arg;
	/* Assume the I/O was successful */
	sequence->is_completed = 1;
	/* See if an error occurred. If so, display information
	 * about it, and set completion value so that I/O
	 * caller is aware that an error occurred.
	 */
	if (spdk_nvme_cpl_is_error(completion)) {
		spdk_nvme_qpair_print_completion(sequence->ns_entry->qpair, (struct spdk_nvme_cpl *)completion);
		fprintf(stderr, "I/O error status: %s\n", spdk_nvme_cpl_get_status_string(&completion->status));
		fprintf(stderr, "Read I/O failed, aborting run\n");
		sequence->is_completed = 2;
		exit(1);
	}

	/*
	 * The read I/O has completed.  Print the contents of the
	 *  buffer, free the buffer, then mark the sequence as
	 *  completed.  This will trigger the hello_world() function
	 *  to exit its polling loop.
	 */
	fprintf(stderr, "%s\n", sequence->buf);
	spdk_free(sequence->buf);
}

void SpdkHello::write_complete(void *arg, const struct spdk_nvme_cpl *completion)
{
	struct_hello_world_sequence	*sequence = (struct_hello_world_sequence*)arg;
	struct_ns_entry			*ns_entry = sequence->ns_entry;
	int				rc;

	/* See if an error occurred. If so, display information
	 * about it, and set completion value so that I/O
	 * caller is aware that an error occurred.
	 */
	if (spdk_nvme_cpl_is_error(completion)) {
		spdk_nvme_qpair_print_completion(sequence->ns_entry->qpair, (struct spdk_nvme_cpl *)completion);
		fprintf(stderr, "I/O error status: %s\n", spdk_nvme_cpl_get_status_string(&completion->status));
		fprintf(stderr, "Write I/O failed, aborting run\n");
		sequence->is_completed = 2;
		exit(1);
	}
	/*
	 * The write I/O has completed.  Free the buffer associated with
	 *  the write I/O and allocate a new zeroed buffer for reading
	 *  the data back from the NVMe namespace.
	 */
	if (sequence->using_cmb_io) {
		spdk_nvme_ctrlr_unmap_cmb(ns_entry->ctrlr);
	} else {
		spdk_free(sequence->buf);
	}
	sequence->buf = (char*)spdk_zmalloc(0x1000, 0x1000, NULL, SPDK_ENV_SOCKET_ID_ANY, SPDK_MALLOC_DMA);

	rc = spdk_nvme_ns_cmd_read(ns_entry->ns, ns_entry->qpair, sequence->buf,
				   1, /* LBA start */
				   1, /* number of LBAs */
				   read_complete, (void *)sequence, 0);
	if (rc != 0) {
		fprintf(stderr, "starting read I/O failed\n");
		exit(1);
	}
}

void SpdkHello::hello_world(void)
{
	struct_hello_world_sequence	sequence;
	int				rc;
	size_t				sz;

	/*
	 * Allocate an I/O qpair that we can use to submit read/write requests
	 *  to namespaces on the controller.  NVMe controllers typically support
	 *  many qpairs per controller.  Any I/O qpair allocated for a controller
	 *  can submit I/O to any namespace on that controller.
	 *
	 * The SPDK NVMe driver provides no synchronization for qpair accesses -
	 *  the application must ensure only a single thread submits I/O to a
	 *  qpair, and that same thread must also check for completions on that
	 *  qpair.  This enables extremely efficient I/O processing by making all
	 *  I/O operations completely lockless.
	 */
	_g_namespaces->qpair = spdk_nvme_ctrlr_alloc_io_qpair(_g_namespaces->ctrlr, NULL, 0);
	if (_g_namespaces->qpair == NULL) {
		fprintf(stderr, "ERROR: spdk_nvme_ctrlr_alloc_io_qpair() failed\n");
		return;
	}

	/*
	 * Use spdk_dma_zmalloc to allocate a 4KB zeroed buffer.  This memory
	 * will be pinned, which is required for data buffers used for SPDK NVMe
	 * I/O operations.
	 */
	sequence.using_cmb_io = 1;
	sequence.buf = (char*)spdk_nvme_ctrlr_map_cmb(_g_namespaces->ctrlr, &sz);
	if (sequence.buf == NULL || sz < 0x1000) {
		sequence.using_cmb_io = 0;
		sequence.buf = (char*)spdk_zmalloc(0x1000, 0x1000, NULL, SPDK_ENV_SOCKET_ID_ANY, SPDK_MALLOC_DMA);
	}
	if (sequence.buf == NULL) {
		fprintf(stderr, "ERROR: write buffer allocation failed\n");
		return;
	}
	if (sequence.using_cmb_io) {
		fprintf(stderr, "INFO: using controller memory buffer for IO\n");
	} else {
		fprintf(stderr, "INFO: using host memory buffer for IO\n");
	}
	sequence.is_completed = 0;
	sequence.ns_entry = _g_namespaces;

	/*
	 * Print "Hello world!" to sequence.buf.  We will write this data to LBA
	 *  0 on the namespace, and then later read it back into a separate buffer
	 *  to demonstrate the full I/O path.
	 */
	snprintf(sequence.buf, 0x1000, "%s", "Hello world! wowo!!\n");

	/*
	 * Write the data buffer to LBA 0 of this namespace.  "write_complete" and
	 *  "&sequence" are specified as the completion callback function and
	 *  argument respectively.  write_complete() will be called with the
	 *  value of &sequence as a parameter when the write I/O is completed.
	 *  This allows users to potentially specify different completion
	 *  callback routines for each I/O, as well as pass a unique handle
	 *  as an argument so the application knows which I/O has completed.
	 *
	 * Note that the SPDK NVMe driver will only check for completions
	 *  when the application calls spdk_nvme_qpair_process_completions().
	 *  It is the responsibility of the application to trigger the polling
	 *  process.
	 */
	rc = spdk_nvme_ns_cmd_write(_g_namespaces->ns, _g_namespaces->qpair, sequence.buf,
				    2, /* LBA start */
				    1, /* number of LBAs */
				    write_complete, &sequence, 0);
	if (rc != 0) {
		fprintf(stderr, "starting write I/O failed\n");
		exit(1);
	}

	/*
	 * Poll for completions.  0 here means process all available completions.
	 *  In certain usage models, the caller may specify a positive integer
	 *  instead of 0 to signify the maximum number of completions it should
	 *  process.  This function will never block - if there are no
	 *  completions pending on the specified qpair, it will return immediately.
	 *
	 * When the write I/O completes, write_complete() will submit a new I/O
	 *  to read LBA 0 into a separate buffer, specifying read_complete() as its
	 *  completion routine.  When the read I/O completes, read_complete() will
	 *  print the buffer contents and set sequence.is_completed = 1.  That will
	 *  break this loop and then exit the program.
	 */
	while (!sequence.is_completed) {
		spdk_nvme_qpair_process_completions(_g_namespaces->qpair, 0);
	}

	/*
	 * Free the I/O qpair.  This typically is done when an application exits.
	 *  But SPDK does support freeing and then reallocating qpairs during
	 *  operation.  It is the responsibility of the caller to ensure all
	 *  pending I/O are completed before trying to free the qpair.
	 */
	spdk_nvme_ctrlr_free_io_qpair(_g_namespaces->qpair);
}

bool SpdkHello::probe_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	 struct spdk_nvme_ctrlr_opts *opts)
{
	if (cb_ctx && opts) fprintf(stderr, "\n");
	fprintf(stderr, "Attaching to %s\n", trid->traddr);
	return true;
}

void SpdkHello::attach_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	  struct spdk_nvme_ctrlr *ctrlr, const struct spdk_nvme_ctrlr_opts *opts)
{
	if (cb_ctx && opts) fprintf(stderr, "\n");
	int nsid, num_ns;
	struct_ctrlr_entry *entry;
	struct spdk_nvme_ns *ns;
	const struct spdk_nvme_ctrlr_data *cdata;
	entry = (struct_ctrlr_entry*)malloc(sizeof(struct_ctrlr_entry));
	if (entry == NULL) {
		perror("ctrlr_entry malloc");
		exit(1);
	}
	fprintf(stderr, "Attached to %s\n", trid->traddr);

	/*
	 * spdk_nvme_ctrlr is the logical abstraction in SPDK for an NVMe
	 *  controller.  During initialization, the IDENTIFY data for the
	 *  controller is read using an NVMe admin command, and that data
	 *  can be retrieved using spdk_nvme_ctrlr_get_data() to get
	 *  detailed information on the controller.  Refer to the NVMe
	 *  specification for more details on IDENTIFY for NVMe controllers.
	 */
	cdata = spdk_nvme_ctrlr_get_data(ctrlr);

	snprintf(entry->name, sizeof(entry->name), "%-20.20s (%-20.20s)", cdata->mn, cdata->sn);

	entry->ctrlr = ctrlr;
	if (ctrlr == nullptr)
		fprintf(stderr, "no NVMe controllers found\n");
	_g_controllers = entry;

	/*
	 * Each controller has one or more namespaces.  An NVMe namespace is basically
	 *  equivalent to a SCSI LUN.  The controller's IDENTIFY data tells us how
	 *  many namespaces exist on the controller.  For Intel(R) P3X00 controllers,
	 *  it will just be one namespace.
	 *
	 * Note that in NVMe, namespace IDs start at 1, not 0.
	 */
	num_ns = spdk_nvme_ctrlr_get_num_ns(ctrlr);
	fprintf(stderr, "Using controller %s with %d namespaces.\n", entry->name, num_ns);
	for (nsid = 1; nsid <= num_ns; nsid++) {
		ns = spdk_nvme_ctrlr_get_ns(ctrlr, nsid);
		if (ns == NULL) {
			continue;
		}
		struct_ns_entry *entry2;

		if (!spdk_nvme_ns_is_active(ns)) {
			return;
		}

		entry2 = (struct_ns_entry*)malloc(sizeof(struct_ns_entry));
		if (entry2 == NULL) {
			perror("ns_entry malloc");
			exit(1);
		}

		entry2->ctrlr = ctrlr;
		entry2->ns = ns;
		_g_namespaces = entry2;

		fprintf(stderr, "  Namespace ID: %d size: %juGB\n", spdk_nvme_ns_get_id(ns),
		       spdk_nvme_ns_get_size(ns) / 1000000000);
	}
}

void SpdkHello::cleanup(void)
{
	struct spdk_nvme_detach_ctx *detach_ctx = NULL;

	free(_g_namespaces);

	spdk_nvme_detach_async(_g_controllers->ctrlr, &detach_ctx);
	free(_g_controllers);

	if (detach_ctx) {
		spdk_nvme_detach_poll(detach_ctx);
	}
}
