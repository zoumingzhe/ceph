import { Component, Input, OnInit } from '@angular/core';
import { FormArray, FormControl, NgForm, Validators } from '@angular/forms';

import _ from 'lodash';

import { NfsService } from '../../../shared/api/nfs.service';
import { Icons } from '../../../shared/enum/icons.enum';
import { CdFormGroup } from '../../../shared/forms/cd-form-group';

@Component({
  selector: 'cd-nfs-form-client',
  templateUrl: './nfs-form-client.component.html',
  styleUrls: ['./nfs-form-client.component.scss']
})
export class NfsFormClientComponent implements OnInit {
  @Input()
  form: CdFormGroup;

  @Input()
  clients: any[];

  nfsSquash: any[] = this.nfsService.nfsSquash;
  nfsAccessType: any[] = this.nfsService.nfsAccessType;
  icons = Icons;

  constructor(private nfsService: NfsService) {}

  ngOnInit() {
    _.forEach(this.clients, (client) => {
      const fg = this.addClient();
      fg.patchValue(client);
    });
  }

  getNoAccessTypeDescr() {
    if (this.form.getValue('access_type')) {
      return `${this.form.getValue('access_type')} ${$localize`(inherited from global config)`}`;
    }
    return $localize`-- Select the access type --`;
  }

  getAccessTypeHelp(index: number) {
    const accessTypeItem = this.nfsAccessType.find((currentAccessTypeItem) => {
      return this.getValue(index, 'access_type') === currentAccessTypeItem.value;
    });
    return _.isObjectLike(accessTypeItem) ? accessTypeItem.help : '';
  }

  getNoSquashDescr() {
    if (this.form.getValue('squash')) {
      return `${this.form.getValue('squash')} (${$localize`inherited from global config`})`;
    }
    return $localize`-- Select what kind of user id squashing is performed --`;
  }

  addClient() {
    const clients = this.form.get('clients') as FormArray;

    const REGEX_IP = `(([0-9]{1,3})\\.([0-9]{1,3})\\.([0-9]{1,3})\.([0-9]{1,3})([/](\\d|[1-2]\\d|3[0-2]))?)`;
    const REGEX_LIST_IP = `${REGEX_IP}([ ,]{1,2}${REGEX_IP})*`;

    const fg = new CdFormGroup({
      addresses: new FormControl('', {
        validators: [Validators.required, Validators.pattern(REGEX_LIST_IP)]
      }),
      access_type: new FormControl(''),
      squash: new FormControl('')
    });

    clients.push(fg);
    return fg;
  }

  removeClient(index: number) {
    const clients = this.form.get('clients') as FormArray;
    clients.removeAt(index);
  }

  showError(index: number, control: string, formDir: NgForm, x: string) {
    return (<any>this.form.controls.clients).controls[index].showError(control, formDir, x);
  }

  getValue(index: number, control: string) {
    const clients = this.form.get('clients') as FormArray;
    const client = clients.at(index) as CdFormGroup;
    return client.getValue(control);
  }

  trackByFn(index: number) {
    return index;
  }
}
