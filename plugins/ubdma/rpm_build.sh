#!/bin/bash
current_dir=$(pwd)
rm -rf ./rpmbuild
mkdir -p rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
tar -czvf ub_dma-1.0.0.tar.gz src/* && mv ub_dma-1.0.0.tar.gz rpmbuild/SOURCES
rpmbuild --define "_topdir $current_dir/rpmbuild" -bb ub_dma.spec