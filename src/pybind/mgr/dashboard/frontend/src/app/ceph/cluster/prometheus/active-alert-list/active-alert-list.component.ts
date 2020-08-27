import { Component, Inject, OnInit, TemplateRef, ViewChild } from '@angular/core';

import { PrometheusService } from '../../../../shared/api/prometheus.service';
import { CellTemplate } from '../../../../shared/enum/cell-template.enum';
import { Icons } from '../../../../shared/enum/icons.enum';
import { CdTableAction } from '../../../../shared/models/cd-table-action';
import { CdTableColumn } from '../../../../shared/models/cd-table-column';
import { CdTableSelection } from '../../../../shared/models/cd-table-selection';
import { Permission } from '../../../../shared/models/permissions';
import { CdDatePipe } from '../../../../shared/pipes/cd-date.pipe';
import { AuthStorageService } from '../../../../shared/services/auth-storage.service';
import { PrometheusAlertService } from '../../../../shared/services/prometheus-alert.service';
import { URLBuilderService } from '../../../../shared/services/url-builder.service';
import { PrometheusListHelper } from '../prometheus-list-helper';

const BASE_URL = 'silences'; // as only silence actions can be used

@Component({
  selector: 'cd-active-alert-list',
  providers: [{ provide: URLBuilderService, useValue: new URLBuilderService(BASE_URL) }],
  templateUrl: './active-alert-list.component.html',
  styleUrls: ['./active-alert-list.component.scss']
})
export class ActiveAlertListComponent extends PrometheusListHelper implements OnInit {
  @ViewChild('externalLinkTpl', { static: true })
  externalLinkTpl: TemplateRef<any>;
  columns: CdTableColumn[];
  tableActions: CdTableAction[];
  permission: Permission;
  selection = new CdTableSelection();
  icons = Icons;
  customCss = {
    'badge badge-danger': 'active',
    'badge badge-warning': 'unprocessed',
    'badge badge-info': 'suppressed'
  };

  constructor(
    // NotificationsComponent will refresh all alerts every 5s (No need to do it here as well)
    private authStorageService: AuthStorageService,
    public prometheusAlertService: PrometheusAlertService,
    private urlBuilder: URLBuilderService,
    private cdDatePipe: CdDatePipe,
    @Inject(PrometheusService) prometheusService: PrometheusService
  ) {
    super(prometheusService);
    this.permission = this.authStorageService.getPermissions().prometheus;
    this.tableActions = [
      {
        permission: 'create',
        canBePrimary: (selection: CdTableSelection) => selection.hasSingleSelection,
        disable: (selection: CdTableSelection) =>
          !selection.hasSingleSelection || selection.first().cdExecuting,
        icon: Icons.add,
        routerLink: () =>
          '/monitoring' + this.urlBuilder.getCreateFrom(this.selection.first().fingerprint),
        name: $localize`Create Silence`
      }
    ];
  }

  ngOnInit() {
    super.ngOnInit();
    this.columns = [
      {
        name: $localize`Name`,
        prop: 'labels.alertname',
        flexGrow: 2
      },
      {
        name: $localize`Job`,
        prop: 'labels.job',
        flexGrow: 2
      },
      {
        name: $localize`Severity`,
        prop: 'labels.severity'
      },
      {
        name: $localize`State`,
        prop: 'status.state',
        cellTransformation: CellTemplate.classAdding
      },
      {
        name: $localize`Started`,
        prop: 'startsAt',
        pipe: this.cdDatePipe
      },
      {
        name: $localize`URL`,
        prop: 'generatorURL',
        sortable: false,
        cellTemplate: this.externalLinkTpl
      }
    ];
  }

  updateSelection(selection: CdTableSelection) {
    this.selection = selection;
  }
}
