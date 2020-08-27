import {
  Component,
  EventEmitter,
  Input,
  OnDestroy,
  OnInit,
  Output,
  ViewChild
} from '@angular/core';

import _ from 'lodash';
import { Subscription } from 'rxjs';

import { OrchestratorService } from '../../../../shared/api/orchestrator.service';
import { FormModalComponent } from '../../../../shared/components/form-modal/form-modal.component';
import { TableComponent } from '../../../../shared/datatable/table/table.component';
import { CellTemplate } from '../../../../shared/enum/cell-template.enum';
import { Icons } from '../../../../shared/enum/icons.enum';
import { NotificationType } from '../../../../shared/enum/notification-type.enum';
import { CdTableAction } from '../../../../shared/models/cd-table-action';
import { CdTableColumn } from '../../../../shared/models/cd-table-column';
import { CdTableColumnFiltersChange } from '../../../../shared/models/cd-table-column-filters-change';
import { CdTableSelection } from '../../../../shared/models/cd-table-selection';
import { OrchestratorFeature } from '../../../../shared/models/orchestrator.enum';
import { OrchestratorStatus } from '../../../../shared/models/orchestrator.interface';
import { Permission } from '../../../../shared/models/permissions';
import { DimlessBinaryPipe } from '../../../../shared/pipes/dimless-binary.pipe';
import { AuthStorageService } from '../../../../shared/services/auth-storage.service';
import { ModalService } from '../../../../shared/services/modal.service';
import { NotificationService } from '../../../../shared/services/notification.service';
import { InventoryDevice } from './inventory-device.model';

@Component({
  selector: 'cd-inventory-devices',
  templateUrl: './inventory-devices.component.html',
  styleUrls: ['./inventory-devices.component.scss']
})
export class InventoryDevicesComponent implements OnInit, OnDestroy {
  @ViewChild(TableComponent, { static: true })
  table: TableComponent;

  // Devices
  @Input() devices: InventoryDevice[] = [];

  // Do not display these columns
  @Input() hiddenColumns: string[] = [];

  // Show filters for these columns, specify empty array to disable
  @Input() filterColumns = [
    'hostname',
    'human_readable_type',
    'available',
    'sys_api.vendor',
    'sys_api.model',
    'sys_api.size'
  ];

  // Device table row selection type
  @Input() selectionType: string = undefined;

  @Output() filterChange = new EventEmitter<CdTableColumnFiltersChange>();

  @Output() fetchInventory = new EventEmitter();

  icons = Icons;
  columns: Array<CdTableColumn> = [];
  selection: CdTableSelection = new CdTableSelection();
  permission: Permission;
  tableActions: CdTableAction[];
  fetchInventorySub: Subscription;

  @Input() orchStatus: OrchestratorStatus = undefined;

  actionOrchFeatures = {
    identify: [OrchestratorFeature.DEVICE_BLINK_LIGHT]
  };

  constructor(
    private authStorageService: AuthStorageService,
    private dimlessBinary: DimlessBinaryPipe,
    private modalService: ModalService,
    private notificationService: NotificationService,
    private orchService: OrchestratorService
  ) {}

  ngOnInit() {
    this.permission = this.authStorageService.getPermissions().osd;
    this.tableActions = [
      {
        permission: 'update',
        icon: Icons.show,
        click: () => this.identifyDevice(),
        name: $localize`Identify`,
        disable: (selection: CdTableSelection) => this.getDisable('identify', selection),
        canBePrimary: (selection: CdTableSelection) => !selection.hasSingleSelection,
        visible: () => _.isString(this.selectionType)
      }
    ];
    const columns = [
      {
        name: $localize`Hostname`,
        prop: 'hostname',
        flexGrow: 1
      },
      {
        name: $localize`Device path`,
        prop: 'path',
        flexGrow: 1
      },
      {
        name: $localize`Type`,
        prop: 'human_readable_type',
        flexGrow: 1,
        cellTransformation: CellTemplate.badge,
        customTemplateConfig: {
          map: {
            hdd: { value: 'HDD', class: 'badge-hdd' },
            ssd: { value: 'SSD', class: 'badge-ssd' }
          }
        }
      },
      {
        name: $localize`Available`,
        prop: 'available',
        flexGrow: 1,
        cellClass: 'text-center',
        cellTransformation: CellTemplate.checkIcon
      },
      {
        name: $localize`Vendor`,
        prop: 'sys_api.vendor',
        flexGrow: 1
      },
      {
        name: $localize`Model`,
        prop: 'sys_api.model',
        flexGrow: 1
      },
      {
        name: $localize`Size`,
        prop: 'sys_api.size',
        flexGrow: 1,
        pipe: this.dimlessBinary
      },
      {
        name: $localize`OSDs`,
        prop: 'osd_ids',
        flexGrow: 1,
        cellTransformation: CellTemplate.badge,
        customTemplateConfig: {
          class: 'badge-dark',
          prefix: 'osd.'
        }
      }
    ];

    this.columns = columns.filter((col: any) => {
      return !this.hiddenColumns.includes(col.prop);
    });

    // init column filters
    _.forEach(this.filterColumns, (prop) => {
      const col = _.find(this.columns, { prop: prop });
      if (col) {
        col.filterable = true;
      }
    });

    if (this.fetchInventory.observers.length > 0) {
      this.fetchInventorySub = this.table.fetchData.subscribe(() => {
        this.fetchInventory.emit();
      });
    }
  }

  ngOnDestroy() {
    if (this.fetchInventorySub) {
      this.fetchInventorySub.unsubscribe();
    }
  }

  onColumnFiltersChanged(event: CdTableColumnFiltersChange) {
    this.filterChange.emit(event);
  }

  getDisable(action: 'identify', selection: CdTableSelection): boolean | string {
    if (!selection.hasSingleSelection) {
      return true;
    }
    return this.orchService.getTableActionDisableDesc(
      this.orchStatus,
      this.actionOrchFeatures[action]
    );
  }

  updateSelection(selection: CdTableSelection) {
    this.selection = selection;
  }

  identifyDevice() {
    const selected = this.selection.first();
    const hostname = selected.hostname;
    const device = selected.path || selected.device_id;
    this.modalService.show(FormModalComponent, {
      titleText: $localize`Identify device ${device}`,
      message: $localize`Please enter the duration how long to blink the LED.`,
      fields: [
        {
          type: 'select',
          name: 'duration',
          value: 300,
          required: true,
          typeConfig: {
            options: [
              { text: $localize`1 minute`, value: 60 },
              { text: $localize`2 minutes`, value: 120 },
              { text: $localize`5 minutes`, value: 300 },
              { text: $localize`10 minutes`, value: 600 },
              { text: $localize`15 minutes`, value: 900 }
            ]
          }
        }
      ],
      submitButtonText: $localize`Execute`,
      onSubmit: (values: any) => {
        this.orchService.identifyDevice(hostname, device, values.duration).subscribe(() => {
          this.notificationService.show(
            NotificationType.success,
            $localize`Identifying '${device}' started on host '${hostname}'`
          );
        });
      }
    });
  }
}
