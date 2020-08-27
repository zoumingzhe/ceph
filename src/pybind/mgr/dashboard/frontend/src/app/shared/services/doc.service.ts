import { Injectable } from '@angular/core';

import { BehaviorSubject, Subscription } from 'rxjs';
import { filter, first, map } from 'rxjs/operators';

import { CephReleaseNamePipe } from '../pipes/ceph-release-name.pipe';
import { SummaryService } from './summary.service';

@Injectable({
  providedIn: 'root'
})
export class DocService {
  private releaseDataSource = new BehaviorSubject<string>(null);
  releaseData$ = this.releaseDataSource.asObservable();

  constructor(
    private summaryservice: SummaryService,
    private cephReleaseNamePipe: CephReleaseNamePipe
  ) {
    this.summaryservice.subscribeOnce((summary) => {
      const releaseName = this.cephReleaseNamePipe.transform(summary.version);
      this.releaseDataSource.next(releaseName);
    });
  }

  urlGenerator(release: string, section: string): string {
    const domain = `http://docs.ceph.com/docs/${release}/`;

    const sections = {
      iscsi: `${domain}mgr/dashboard/#enabling-iscsi-management`,
      prometheus: `${domain}mgr/dashboard/#enabling-prometheus-alerting`,
      'nfs-ganesha': `${domain}mgr/dashboard/#configuring-nfs-ganesha-in-the-dashboard`,
      'rgw-nfs': `${domain}radosgw/nfs`,
      rgw: `${domain}mgr/dashboard/#enabling-the-object-gateway-management-frontend`,
      dashboard: `${domain}mgr/dashboard`,
      grafana: `${domain}mgr/dashboard/#enabling-the-embedding-of-grafana-dashboards`,
      orch: `${domain}mgr/orchestrator`,
      pgs: `http://ceph.com/pgcalc`
    };

    return sections[section];
  }

  subscribeOnce(
    section: string,
    next: (release: string) => void,
    error?: (error: any) => void
  ): Subscription {
    return this.releaseData$
      .pipe(
        filter((value) => !!value),
        map((release) => this.urlGenerator(release, section)),
        first()
      )
      .subscribe(next, error);
  }
}
