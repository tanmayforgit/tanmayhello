/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/stat.h>
#include <getopt.h>

#include "kerncompat.h"
#include "ctree.h"
#include "ioctl.h"
#include "utils.h"

#include "commands.h"

static const char * const device_cmd_group_usage[] = {
	"btrfs device <command> [<args>]",
	NULL
};

static const char * const cmd_add_dev_usage[] = {
	"btrfs device add [options] <device> [<device>...] <path>",
	"Add a device to a filesystem",
	"-K|--nodiscard    do not perform whole device TRIM",
	"-f|--force        force overwrite existing filesystem on the disk",
	NULL
};

static int cmd_add_dev(int argc, char **argv)
{
	char	*mntpnt;
	int	i, fdmnt, ret=0, e;
	DIR	*dirstream = NULL;
	int discard = 1;
	int force = 0;
	char estr[100];

	while (1) {
		int long_index;
		static struct option long_options[] = {
			{ "nodiscard", optional_argument, NULL, 'K'},
			{ "force", no_argument, NULL, 'f'},
			{ 0, 0, 0, 0 }
		};
		int c = getopt_long(argc, argv, "Kf", long_options,
					&long_index);
		if (c < 0)
			break;
		switch (c) {
		case 'K':
			discard = 0;
			break;
		case 'f':
			force = 1;
			break;
		default:
			usage(cmd_add_dev_usage);
		}
	}

	argc = argc - optind;

	if (check_argc_min(argc, 2))
		usage(cmd_add_dev_usage);

	mntpnt = argv[optind + argc - 1];

	fdmnt = open_file_or_dir(mntpnt, &dirstream);
	if (fdmnt < 0) {
		fprintf(stderr, "ERROR: can't access to '%s'\n", mntpnt);
		return 1;
	}

	for (i = optind; i < optind + argc - 1; i++){
		struct btrfs_ioctl_vol_args ioctl_args;
		int	devfd, res;
		u64 dev_block_count = 0;
		int mixed = 0;

		res = test_dev_for_mkfs(argv[i], force, estr);
		if (res) {
			fprintf(stderr, "%s", estr);
			ret++;
			continue;
		}

		devfd = open(argv[i], O_RDWR);
		if (devfd < 0) {
			fprintf(stderr, "ERROR: Unable to open device '%s'\n", argv[i]);
			ret++;
			continue;
		}

		res = btrfs_prepare_device(devfd, argv[i], 1, &dev_block_count,
					   0, &mixed, discard);
		if (res) {
			fprintf(stderr, "ERROR: Unable to init '%s'\n", argv[i]);
			close(devfd);
			ret++;
			continue;
		}
		close(devfd);

		strncpy_null(ioctl_args.name, argv[i]);
		res = ioctl(fdmnt, BTRFS_IOC_ADD_DEV, &ioctl_args);
		e = errno;
		if(res<0){
			fprintf(stderr, "ERROR: error adding the device '%s' - %s\n",
				argv[i], strerror(e));
			ret++;
		}

	}

	close_file_or_dir(fdmnt, dirstream);
	return !!ret;
}

static const char * const cmd_rm_dev_usage[] = {
	"btrfs device delete <device> [<device>...] <path>",
	"Remove a device from a filesystem",
	NULL
};

static int cmd_rm_dev(int argc, char **argv)
{
	char	*mntpnt;
	int	i, fdmnt, ret=0, e;
	DIR	*dirstream = NULL;

	if (check_argc_min(argc, 3))
		usage(cmd_rm_dev_usage);

	mntpnt = argv[argc - 1];

	fdmnt = open_file_or_dir(mntpnt, &dirstream);
	if (fdmnt < 0) {
		fprintf(stderr, "ERROR: can't access to '%s'\n", mntpnt);
		return 1;
	}

	for(i=1 ; i < argc - 1; i++ ){
		struct	btrfs_ioctl_vol_args arg;
		int	res;

		strncpy_null(arg.name, argv[i]);
		res = ioctl(fdmnt, BTRFS_IOC_RM_DEV, &arg);
		e = errno;
		if (res > 0) {
			fprintf(stderr,
				"ERROR: error removing the device '%s' - %s\n",
				argv[i], btrfs_err_str(res));
			ret++;
		} else if (res < 0) {
			fprintf(stderr,
				"ERROR: error removing the device '%s' - %s\n",
				argv[i], strerror(e));
			ret++;
		}
	}

	close_file_or_dir(fdmnt, dirstream);
	return !!ret;
}

static const char * const cmd_scan_dev_usage[] = {
	"btrfs device scan [<--all-devices>|<device> [<device>...]]",
	"Scan devices for a btrfs filesystem",
	NULL
};

static int cmd_scan_dev(int argc, char **argv)
{
	int	i, fd, e;
	int	where = BTRFS_SCAN_LBLKID;
	int	devstart = 1;

	if( argc > 1 && !strcmp(argv[1],"--all-devices")){
		if (check_argc_max(argc, 2))
			usage(cmd_scan_dev_usage);

		where = BTRFS_SCAN_DEV;
		devstart += 1;
	}

	if(argc<=devstart){
		int ret;
		printf("Scanning for Btrfs filesystems\n");
		ret = scan_for_btrfs(where, BTRFS_UPDATE_KERNEL);
		if (ret){
			fprintf(stderr, "ERROR: error %d while scanning\n", ret);
			return 1;
		}
		return 0;
	}

	fd = open("/dev/btrfs-control", O_RDWR);
	if (fd < 0) {
		perror("failed to open /dev/btrfs-control");
		return 1;
	}

	for( i = devstart ; i < argc ; i++ ){
		struct btrfs_ioctl_vol_args args;
		int ret;

		printf("Scanning for Btrfs filesystems in '%s'\n", argv[i]);

		strncpy_null(args.name, argv[i]);
		/*
		 * FIXME: which are the error code returned by this ioctl ?
		 * it seems that is impossible to understand if there no is
		 * a btrfs filesystem from an I/O error !!!
		 */
		ret = ioctl(fd, BTRFS_IOC_SCAN_DEV, &args);
		e = errno;

		if( ret < 0 ){
			close(fd);
			fprintf(stderr, "ERROR: unable to scan the device '%s' - %s\n",
				argv[i], strerror(e));
			return 1;
		}
	}

	close(fd);
	return 0;
}

static const char * const cmd_ready_dev_usage[] = {
	"btrfs device ready <device>",
	"Check device to see if it has all of it's devices in cache for mounting",
	NULL
};

static int cmd_ready_dev(int argc, char **argv)
{
	struct	btrfs_ioctl_vol_args args;
	int	fd;
	int	ret;

	if (check_argc_min(argc, 2))
		usage(cmd_ready_dev_usage);

	fd = open("/dev/btrfs-control", O_RDWR);
	if (fd < 0) {
		perror("failed to open /dev/btrfs-control");
		return 1;
	}

	strncpy(args.name, argv[argc - 1], BTRFS_PATH_NAME_MAX);
	ret = ioctl(fd, BTRFS_IOC_DEVICES_READY, &args);
	if (ret < 0) {
		fprintf(stderr, "ERROR: unable to determine if the device '%s'"
			" is ready for mounting - %s\n", argv[argc - 1],
			strerror(errno));
		ret = 1;
	}

	close(fd);
	return ret;
}

static const char * const cmd_dev_stats_usage[] = {
	"btrfs device stats [-z] <path>|<device>",
	"Show current device IO stats. -z to reset stats afterwards.",
	NULL
};

static int cmd_dev_stats(int argc, char **argv)
{
	char *dev_path;
	struct btrfs_ioctl_fs_info_args fi_args;
	struct btrfs_ioctl_dev_info_args *di_args = NULL;
	int ret;
	int fdmnt;
	int i;
	int c;
	int err = 0;
	__u64 flags = 0;
	DIR *dirstream = NULL;

	optind = 1;
	while ((c = getopt(argc, argv, "z")) != -1) {
		switch (c) {
		case 'z':
			flags = BTRFS_DEV_STATS_RESET;
			break;
		case '?':
		default:
			fprintf(stderr, "ERROR: device stat args invalid.\n"
					" device stat [-z] <path>|<device>\n"
					" -z  to reset stats after reading.\n");
			return 1;
		}
	}

	if (optind + 1 != argc) {
		fprintf(stderr, "ERROR: device stat needs path|device as single"
			" argument\n");
		return 1;
	}

	dev_path = argv[optind];

	fdmnt = open_path_or_dev_mnt(dev_path, &dirstream);

	if (fdmnt < 0) {
		fprintf(stderr, "ERROR: can't access '%s'\n", dev_path);
		return 1;
	}

	ret = get_fs_info(dev_path, &fi_args, &di_args);
	if (ret) {
		fprintf(stderr, "ERROR: getting dev info for devstats failed: "
				"%s\n", strerror(-ret));
		err = 1;
		goto out;
	}
	if (!fi_args.num_devices) {
		fprintf(stderr, "ERROR: no devices found\n");
		err = 1;
		goto out;
	}

	for (i = 0; i < fi_args.num_devices; i++) {
		struct btrfs_ioctl_get_dev_stats args = {0};
		__u8 path[BTRFS_DEVICE_PATH_NAME_MAX + 1];

		strncpy((char *)path, (char *)di_args[i].path,
			BTRFS_DEVICE_PATH_NAME_MAX);
		path[BTRFS_DEVICE_PATH_NAME_MAX] = '\0';

		args.devid = di_args[i].devid;
		args.nr_items = BTRFS_DEV_STAT_VALUES_MAX;
		args.flags = flags;

		if (ioctl(fdmnt, BTRFS_IOC_GET_DEV_STATS, &args) < 0) {
			fprintf(stderr,
				"ERROR: ioctl(BTRFS_IOC_GET_DEV_STATS) on %s failed: %s\n",
				path, strerror(errno));
			err = 1;
		} else {
			if (args.nr_items >= BTRFS_DEV_STAT_WRITE_ERRS + 1)
				printf("[%s].write_io_errs   %llu\n",
				       path,
				       (unsigned long long) args.values[
					BTRFS_DEV_STAT_WRITE_ERRS]);
			if (args.nr_items >= BTRFS_DEV_STAT_READ_ERRS + 1)
				printf("[%s].read_io_errs    %llu\n",
				       path,
				       (unsigned long long) args.values[
					BTRFS_DEV_STAT_READ_ERRS]);
			if (args.nr_items >= BTRFS_DEV_STAT_FLUSH_ERRS + 1)
				printf("[%s].flush_io_errs   %llu\n",
				       path,
				       (unsigned long long) args.values[
					BTRFS_DEV_STAT_FLUSH_ERRS]);
			if (args.nr_items >= BTRFS_DEV_STAT_CORRUPTION_ERRS + 1)
				printf("[%s].corruption_errs %llu\n",
				       path,
				       (unsigned long long) args.values[
					BTRFS_DEV_STAT_CORRUPTION_ERRS]);
			if (args.nr_items >= BTRFS_DEV_STAT_GENERATION_ERRS + 1)
				printf("[%s].generation_errs %llu\n",
				       path,
				       (unsigned long long) args.values[
					BTRFS_DEV_STAT_GENERATION_ERRS]);
		}
	}

out:
	free(di_args);
	close_file_or_dir(fdmnt, dirstream);

	return err;
}

const struct cmd_group device_cmd_group = {
	device_cmd_group_usage, NULL, {
		{ "add", cmd_add_dev, cmd_add_dev_usage, NULL, 0 },
		{ "delete", cmd_rm_dev, cmd_rm_dev_usage, NULL, 0 },
		{ "scan", cmd_scan_dev, cmd_scan_dev_usage, NULL, 0 },
		{ "ready", cmd_ready_dev, cmd_ready_dev_usage, NULL, 0 },
		{ "stats", cmd_dev_stats, cmd_dev_stats_usage, NULL, 0 },
		NULL_CMD_STRUCT
	}
};

int cmd_device(int argc, char **argv)
{
	return handle_command_group(&device_cmd_group, argc, argv);
}
