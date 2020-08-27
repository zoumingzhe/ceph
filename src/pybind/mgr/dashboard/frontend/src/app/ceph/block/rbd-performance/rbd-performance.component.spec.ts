import { HttpClientTestingModule } from '@angular/common/http/testing';
import { ComponentFixture, TestBed } from '@angular/core/testing';
import { RouterTestingModule } from '@angular/router/testing';

import { NgbNavModule } from '@ng-bootstrap/ng-bootstrap';

import { configureTestBed } from '../../../../testing/unit-test-helper';
import { SharedModule } from '../../../shared/shared.module';
import { RbdTabsComponent } from '../rbd-tabs/rbd-tabs.component';
import { RbdPerformanceComponent } from './rbd-performance.component';

describe('RbdPerformanceComponent', () => {
  let component: RbdPerformanceComponent;
  let fixture: ComponentFixture<RbdPerformanceComponent>;

  configureTestBed({
    imports: [HttpClientTestingModule, RouterTestingModule, SharedModule, NgbNavModule],
    declarations: [RbdPerformanceComponent, RbdTabsComponent]
  });

  beforeEach(() => {
    fixture = TestBed.createComponent(RbdPerformanceComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
