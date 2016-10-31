//
//  AddSecretViewController.h
//  Mitro
//
//  Created by Aaron Sullivan on 10/30/16.
//  Copyright © 2016 Aquarious, Inc. All rights reserved.
//

#import "Mitro.h"
#import "SecretManager.h"
#import <UIKit/UIKit.h>

@protocol AddSecretDelegate <NSObject>

@optional
- (void)didCancel;
- (void)didAddSecret;

@end

@interface AddSecretViewController : UIViewController <UITextViewDelegate, SecretManagerDelegate>

@property (weak, nonatomic) id<AddSecretDelegate> delegate;

@property (weak, nonatomic) IBOutlet UISegmentedControl *secretTypeControl;

@property (weak, nonatomic) IBOutlet UITextField *titleField;
@property (weak, nonatomic) IBOutlet UITextField *urlField;
@property (weak, nonatomic) IBOutlet UITextField *usernameField;
@property (weak, nonatomic) IBOutlet UITextField *passwordField;

@property (weak, nonatomic) IBOutlet UITextView *noteView;

@end
